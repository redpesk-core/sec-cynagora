/*
 * Copyright (C) 2018 "IoT.bzh"
 * Author José Bollo <jose.bollo@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#define _GNU_SOURCE


#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>

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
	const char *name,
	const char *backup
) {
	struct stat st;
	int fd, fdback, rc;
	uint32_t sz, asz;
	void *buffer;

	/* open the backup */
	if (backup == NULL)
		fdback = -1;
	else {
		fdback = open(backup, O_RDWR|O_CREAT, 0600);
		if (fdback < 0)
			goto error;
		rc = flock(fdback, LOCK_EX|LOCK_NB);
		if (rc < 0)
			goto error;
	}

	/* open the file */
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

	/* save name */
	fb->name = strdup(name);
	if (fb->name == NULL)
		goto error4;

	/* done */
	fb->buffer = buffer;
	fb->saved = fb->used = fb->size = sz;
	fb->capacity = asz;
	fb->fd = fd;
	fb->backup = fdback;
	return 0;

error4:
	free(buffer);
error3:
	flock(fd, LOCK_UN);
error2:
	close(fd);
error:
	rc = -errno;
	fprintf(stderr, "can't open file %s: %m", name);
	if (fdback >= 0) {
		flock(fdback, LOCK_UN);
		close(fdback);
	}
	memset(fb, 0, sizeof *fb);
	return rc;
}

/** close the file 'fb' */
void
fbuf_close(
	fbuf_t	*fb
) {
	free(fb->name);
	free(fb->buffer);
	flock(fb->fd, LOCK_UN);
	close(fb->fd);
	if (fb->backup >= 0) {
		flock(fb->backup, LOCK_UN);
		close(fb->backup);
	}
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
	ssize_t rcs;

	/* don't call me for nothing */
	assert(count);

	/* effective write */
	rcs = pwrite(fb->fd, buffer, (size_t)count, (off_t)offset);
	if (rcs != (ssize_t)count)
		goto error;

	return 0;
error:
	fprintf(stderr, "write of file %s failed: %m", fb->name);
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
	fprintf(stderr, "sync of file %s failed: %m", fb->name);
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
			fprintf(stderr, "alloc %u for file %s failed: %m", capacity, fb->name);
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
	fprintf(stderr, "identification of file %s failed: %m", fb->name);
	return -ENOKEY;
}

/** check or make identification by 'uuid' of file 'fb' */
int
fbuf_open_identify(
	fbuf_t	*fb,
	const char *name,
	const char *backup,
	const char *id,
	uint32_t idlen
) {
	int rc;

	rc = fbuf_open(fb, name, backup);
	if (rc == 0) {
		rc = fbuf_identify(fb, id, idlen);
		if (rc < 0)
			fbuf_close(fb);
	}
	return rc;
}


/* On versions of glibc before 2.27, we must invoke copy_file_range()
  using syscall(2) */
#include <features.h>
#if (__GLIBC__ < 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 27)
#include <syscall.h>
static
loff_t
copy_file_range(
	int fd_in,
	loff_t *off_in,
	int fd_out,
	loff_t *off_out,
	size_t len,
	unsigned int flags
) {
	loff_t rc;

	rc = syscall(__NR_copy_file_range, fd_in, off_in, fd_out,
				off_out, len, flags);
	return rc;
}
#endif


/** make a backup */
int
fbuf_backup(
	fbuf_t	*fb
) {
	int rc;
	size_t sz;
	ssize_t wsz;
	loff_t ino, outo;

	ino = outo = 0;
	sz = fb->size;
	for (;;) {
		wsz = copy_file_range(fb->fd, &ino, fb->backup, &outo, sz, 0);
		if (wsz < 0) {
			if (errno != EINTR)
				return -errno;
		} else {
			sz -= (size_t)wsz;
			if (sz == 0) {
				rc = ftruncate(fb->backup, outo);
				if (rc == 0)
					rc = fsync(fb->backup);
				return rc < 0 ? -errno : 0;
			}
		}
	}
}

/** recover from latest backup */
int
fbuf_recover(
	fbuf_t	*fb
) {
	ssize_t ssz;
	struct stat st;
	int rc;

	/* get the size */
	rc = fstat(fb->backup, &st);
	if (rc < 0)
		return -errno;

	/* ensure space */
	if (st.st_size > UINT32_MAX)
		return -EFBIG;
	rc = fbuf_ensure_capacity(fb, (uint32_t)st.st_size);
	if (rc < 0)
		return rc;

	/* read it */
	ssz = pread(fb->backup, fb->buffer, (size_t)st.st_size, 0); 
	if (ssz < 0)
		return -errno;

	fb->used = (uint32_t)st.st_size;
	fb->saved = fb->size = 0; /* ensure rewrite of restored data */
	return 0;
}

