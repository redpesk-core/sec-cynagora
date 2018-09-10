
#define _GNU_SOURCE


#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>

#include "fbuf.h"

/** compute the size to allocate for ensuring 'sz' bytes */
static
uint32_t
get_asz(
	uint32_t sz
) {
	return (sz & 0xfffffc00) + 0x000004cf;
}

/** open in 'fb' the file of 'name' */
int
fbuf_open(
	fbuf_t	*fb,
	const char *name
) {
	struct stat st;
	int fd, rc;
	uint32_t sz, asz;
	void *buffer;

	/* open the file */
	//fd = open(name, O_RDWR|O_CREAT|O_SYNC|O_DIRECT, 0600);
	fd = open(name, O_RDWR|O_CREAT, 0600);
	if (fd < 0)
		goto error;

	/* lock it */
	rc = flock(fd, LOCK_EX|LOCK_NB);
	if (rc < 0)
		goto error2;

	/* get file stat */
	rc = fstat(fd, &st);
	if (rc < 0)
		goto error3;

	/* check file size */
	if ((off_t)INT32_MAX < st.st_size) {
		errno = EFBIG;
		goto error3;
	}

	/* compute allocation size */
	sz = (uint32_t)st.st_size;
	asz = get_asz(sz);
	buffer = malloc(asz);
	if (buffer == NULL)
		goto error3;

	/* read the file */
	if (read(fd, buffer, (size_t)sz) != (ssize_t)sz)
		goto error4;

	/* done */
	fb->name = name;
	fb->buffer = buffer;
	fb->saved = fb->used = fb->size = sz;
	fb->capacity = asz;
	fb->fd = fd;
	return 0;

error4:
	free(buffer);
error3:
	flock(fd, LOCK_UN);
error2:
	close(fd);
error:
	syslog(LOG_ERR, "can't open file %s: %m", name);
	memset(fb, 0, sizeof *fb);
	return -errno;
}

/** close the file 'fb' */
void
fbuf_close(
	fbuf_t	*fb
) {
	free(fb->buffer);
	flock(fb->fd, LOCK_UN);
	close(fb->fd);
	memset(fb, 0, sizeof *fb);
}

/** write to file 'fb' at 'offset' the 'count' bytes pointed by 'buffer' */
int
fbuf_write(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count,
	uint32_t offset
) {
	off_t rco;
	ssize_t rcs;

	/* don't call me for nothing */
	assert(count);

	/* set write position */
	rco = lseek(fb->fd, (off_t)offset, SEEK_SET);
	if (rco != (off_t)offset)
		goto error;

	/* effective write */
	rcs = write(fb->fd, buffer, (size_t)count);
	if (rcs != (ssize_t)count)
		goto error;

	return 0;
error:
	syslog(LOG_ERR, "write of file %s failed: %m", fb->name);
	return -errno;
}

/** write to file 'fb' the unsaved bytes and flush the content to the file */
int
fbuf_sync(
	fbuf_t	*fb
) {
	int rc;
	bool changed = false;

	/* write unsaved bytes */
	if (fb->used > fb->saved) {
		rc = fbuf_write(fb, fb->buffer + fb->saved, fb->used - fb->saved, fb->saved);
		if (rc < 0)
			return rc;
		fb->saved = fb->used;
		changed = true;
	}

	/* truncate on needed */
	if (fb->used < fb->size) {
		rc = ftruncate(fb->fd, (off_t)fb->used);
		if (rc < 0)
			goto error;
		changed = true;
	}
	fb->size = fb->used;

	/* force synchronisation of the file */
	if (changed) {
		rc = fsync(fb->fd);
		if (rc < 0)
			goto error;
	}

	return 0;
error:
	syslog(LOG_ERR, "sync of file %s failed: %m", fb->name);
	return -errno;
}

/** allocate enough memory in 'fb' to store 'count' bytes */
int
fbuf_ensure_capacity(
	fbuf_t	*fb,
	uint32_t count
) {
	uint32_t capacity;
	void *buffer;

	if (count > fb->capacity) {
		capacity = get_asz(count);
		buffer = realloc(fb->buffer, capacity);
		if (buffer == NULL) {
			syslog(LOG_ERR, "alloc %u for file %s failed: %m", capacity, fb->name);
			return -ENOMEM;
		}
		fb->buffer = buffer;
		fb->capacity = capacity;
	}
	return 0;
}

/** put at 'offset' in the memory of 'fb' the 'count' bytes pointed by 'buffer' */
int
fbuf_put(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count,
	uint32_t offset
) {
	int rc;
	uint32_t end = offset + count;

	/* don't call me for nothing */
	assert(count);

	/* grow as necessary */
	if (end > fb->used) {
		rc = fbuf_ensure_capacity(fb, end);
		if (rc < 0)
			return rc;
		fb->used = end;
	}

	/* copy the data */
	memcpy(fb->buffer + offset, buffer, count);

	/* write the data to the disk */
	if (offset < fb->saved)
		fb->saved = offset;
	return 0;
}

/** append at end in the memory of 'fb' the 'count' bytes pointed by 'buffer' */
int
fbuf_append(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count
) {
	/* don't call me for nothing */
	assert(count);

	return fbuf_put(fb, buffer, count, fb->used);
}

/** check or make identification of file 'fb' by 'id' of 'len' */
int
fbuf_identify(
	fbuf_t	*fb,
	const char *id,
	uint32_t idlen
) {
	/* init if empty */
	if (fb->saved == 0 && fb->used == 0)
		return fbuf_append(fb, id, idlen);

	/* check if not empty */
	if (fb->saved >= idlen && !memcmp(fb->buffer, id, idlen))
		return 0;

	/* bad identification */
	errno = ENOKEY;
	syslog(LOG_ERR, "identification of file %s failed: %m", fb->name);
	return -ENOKEY;
}

/** check or make identification by 'uuid' of file 'fb' */
int
fbuf_open_identify(
	fbuf_t	*fb,
	const char *name,
	const char *id,
	uint32_t idlen
) {
	int rc;

	rc = fbuf_open(fb, name);
	if (rc == 0) {
		rc = fbuf_identify(fb, id, idlen);
		if (rc < 0)
			fbuf_close(fb);
	}
	return rc;
}

