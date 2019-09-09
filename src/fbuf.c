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
static
int
read_file(
	fbuf_t	*fb,
	const char *name
) {
	struct stat st;
	int fd, rc;
	uint32_t sz;

	/* open the file */
	fd = open(name, O_RDWR|O_CREAT, 0600);
	if (fd < 0)
		goto error;

	/* get file stat */
	rc = fstat(fd, &st);
	if (rc < 0)
		goto error2;

	/* check file size */
	if ((off_t)INT32_MAX < st.st_size) {
		errno = EFBIG;
		goto error2;
	}
	sz = (uint32_t)st.st_size;

	/* allocate memory */
	rc = fbuf_ensure_capacity(fb, (uint32_t)st.st_size);
	if (rc < 0)
		goto error2;

	/* read the file */
	if (read(fd, fb->buffer, (size_t)sz) != (ssize_t)sz)
		goto error2;

	/* done */
	fb->used = fb->size = sz;
	close(fd);
	return 0;

error2:
	close(fd);
error:
	rc = -errno;
	fprintf(stderr, "can't read file %s: %m\n", name);
	fb->saved = fb->used = fb->size = 0;
	return rc;
}

/** open in 'fb' the file of 'name' */
int
fbuf_open(
	fbuf_t	*fb,
	const char *name,
	const char *backup
) {
	int rc;
	size_t sz;

	/* reset */
	memset(fb, 0, sizeof *fb);

	/* save name */
	fb->name = strdup(name);
	if (fb->name == NULL)
		goto error;

	/* open the backup */
	if (backup != NULL)
		fb->backup = strdup(backup);
	else {
		sz = strlen(name);
		fb->backup = malloc(sz + 2);
		if (fb->backup != NULL) {
			memcpy(fb->backup, name, sz);
			fb->backup[sz] = '~';
			fb->backup[sz + 1] = 0;
		}
	}
	if (fb->backup == NULL)
		goto error;

	/* read the file */
	rc = read_file(fb, fb->name);
	if (rc < 0)
		goto error;

	fb->saved = fb->used;
	return 0;

error:
	rc = -errno;
	fprintf(stderr, "can't open file %s: %m\n", name);
	fbuf_close(fb);
	return rc;
}

/** close the file 'fb' */
void
fbuf_close(
	fbuf_t	*fb
) {
	free(fb->name);
	free(fb->backup);
	free(fb->buffer);
	memset(fb, 0, sizeof *fb);
}

/** write to file 'fb' the unsaved bytes and flush the content to the file */
int
fbuf_sync(
	fbuf_t	*fb
) {
	ssize_t rcs;
	int rc, fd;

	/* is sync needed? */
	if (fb->used == fb->saved && fb->used == fb->size)
		return 0;

	/* open the file */
	unlink(fb->name);
	fd = creat(fb->name, 0600);
	if (fd < 0)
		goto error;

	/* write unsaved bytes */
	rcs = write(fd, fb->buffer, fb->used);
	close(fd);
	if (rcs < 0)
		goto error;

	fb->size = fb->saved = fb->used;
	return 0;

error:
	rc = -errno;
	fprintf(stderr, "sync of file %s failed: %m\n", fb->name);
	return rc;
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
			fprintf(stderr, "alloc %u for file %s failed: %m\n", capacity, fb->name);
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
	fprintf(stderr, "identification of file %s failed: %m\n", fb->name);
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

/** make a backup */
int
fbuf_backup(
	fbuf_t	*fb
) {
	unlink(fb->backup);
	return link(fb->name, fb->backup);
}

/** recover from latest backup */
int
fbuf_recover(
	fbuf_t	*fb
) {
	int rc;

	rc = read_file(fb, fb->backup);
	fb->saved = 0; /* ensure rewrite of restored data */
	return rc;
}

