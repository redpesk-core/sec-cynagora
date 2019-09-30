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
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF BUFFERED FILES                                           */
/******************************************************************************/
/******************************************************************************/

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

/**
 * compute the size to allocate for ensuring 'sz' bytes
 * @param sz the expected size
 * @return a size greater than sz
 */
static
uint32_t
get_asz(
	uint32_t sz
) {
	uint32_t r = (sz & 0xfffffc00) + 0x000004cf;
	return r > sz ? r : UINT32_MAX;
}

/**
 * Open in 'fb' the file of 'name'
 * @param fb the fbuf
 * @param name the name of the file to read
 * @return 0 on success
 *         -EFBIG if the file is too big
 *         -errno system error
 */
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

/* see fbuf.h */
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
	sz = strlen(name);
	fb->name = malloc(sz + 1);
	if (fb->name == NULL)
		goto error;
	mempcpy(fb->name, name, sz + 1);

	/* open the backup */
	if (backup != NULL)
		fb->backup = strdup(backup);
	else {
		fb->backup = malloc(sz + 2);
		if (fb->backup != NULL) {
			mempcpy(fb->backup, name, sz);
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

	/* any read data is already saved */
	fb->saved = fb->used;
	return 0;

error:
	rc = -errno;
	fprintf(stderr, "can't open file %s: %m\n", name);
	fbuf_close(fb);
	return rc;
}

/* see fbuf.h */
void
fbuf_close(
	fbuf_t	*fb
) {
	free(fb->name);
	free(fb->backup);
	free(fb->buffer);
	memset(fb, 0, sizeof *fb);
}

/* see fbuf.h */
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
	if ((uint32_t)rcs != fb->used)
		goto error; /* TODO: set some errno? */

	fb->size = fb->saved = fb->used;
	return 0;

error:
	rc = -errno;
	fprintf(stderr, "sync of file %s failed: %m\n", fb->name);
	return rc;
}

/* see fbuf.h */
int
fbuf_ensure_capacity(
	fbuf_t	*fb,
	uint32_t capacity
) {
	uint32_t asz;
	void *buffer;

	if (capacity > fb->capacity) {
		asz = get_asz(capacity);
		buffer = realloc(fb->buffer, asz);
		if (buffer == NULL) {
			fprintf(stderr, "alloc %u for file %s failed: %m\n", asz, fb->name);
			return -ENOMEM;
		}
		fb->buffer = buffer;
		fb->capacity = asz;
	}
	return 0;
}

/* see fbuf.h */
int
fbuf_put(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count,
	uint32_t offset
) {
	int rc;
	uint32_t end;

	/* don't call me for nothing */
	assert(count);

	/* grow as necessary */
	end = offset + count;
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

/* see fbuf.h */
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

/* see fbuf.h */
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
	fprintf(stderr, "identification of file %s failed: %m\n", fb->name);
	return -ENOKEY;
}

/* see fbuf.h */
int
fbuf_open_identify(
	fbuf_t	*fb,
	const char *name,
	const char *backup,
	const char *id,
	uint32_t idlen
) {
	int rc;

	/* open the files */
	rc = fbuf_open(fb, name, backup);
	if (rc == 0) {
		/* check identifier */
		rc = fbuf_identify(fb, id, idlen);
		if (rc < 0)
			fbuf_close(fb); /* close if error */
	}
	return rc;
}

/* see fbuf.h */
int
fbuf_backup(
	fbuf_t	*fb
) {
	unlink(fb->backup);
	return link(fb->name, fb->backup);
}

/* see fbuf.h */
int
fbuf_recover(
	fbuf_t	*fb
) {
	int rc;

	rc = read_file(fb, fb->backup);
	fb->saved = 0; /* ensure rewrite of restored data */
	return rc;
}

