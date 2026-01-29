/*
 * Copyright (C) 2018-2026 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
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
#include <errno.h>

#include "fbuf-sysfile.h"
#define FBUF_READ    fbuf_sysfile_read
#define FBUF_SYNC    fbuf_sysfile_sync
#define FBUF_BACKUP  fbuf_sysfile_backup
#define FBUF_RECOVER fbuf_sysfile_recover

#ifndef FBUF_MAX_CAPACITY
#define FBUF_MAX_CAPACITY UINT32_MAX
#endif

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

/* see fbuf.h */
int
fbuf_open(
	fbuf_t	*fb,
	fbuf_type_t type,
	const char *name,
	const char *backup
) {
	int rc;
	size_t sz;

	/* reset */
	memset(fb, 0, sizeof *fb);

	/* set the type */
	fb->type = type;

	/* save name */
	sz = strlen(name);
	fb->name = malloc(sz + 1);
	if (fb->name == NULL) {
		rc = -ENOMEM;
		goto error;
	}
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
	if (fb->backup == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* read the file */
	rc = FBUF_READ(fb);
	if (rc < 0)
		goto error;

	/* any read data is already saved */
	fb->saved = fb->used;
	return 0;

error:
	fprintf(stderr, "can't open file %s: %s\n", name, strerror(-rc));
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
	int rc = 0;

	/* is sync needed? */
	if (fb->used != fb->saved || fb->used != fb->size) {
		rc = FBUF_SYNC(fb);
		if (rc == 0)
			fb->size = fb->saved = fb->used;
	}
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
#ifndef FBUF_MAX_CAPACITY

		if (asz > FBUF_MAX_CAPACITY) {
			fprintf(stderr, "alloc %u %s failed, reach max capacity: %s\n",
					asz, fb->name, strerror(ENOMEM));
			return -ENOMEM;
		}
#endif
		buffer = realloc(fb->buffer, asz);
		if (buffer == NULL) {
			fprintf(stderr, "alloc %u for file %s failed: %s\n",
					asz, fb->name, strerror(ENOMEM));
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
	fprintf(stderr, "identification of file %s failed\n", fb->name);
	return -ENOKEY;
}

/* see fbuf.h */
int
fbuf_open_identify(
	fbuf_t	*fb,
	fbuf_type_t type,
	const char *name,
	const char *backup,
	const char *id,
	uint32_t idlen
) {
	int rc;

	/* open the files */
	rc = fbuf_open(fb, type, name, backup);
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
	int rc;

	if (fb->backuped)
		return 0;
	rc = FBUF_BACKUP(fb);
	fb->backuped = rc == 0;
	return rc;
}

/* see fbuf.h */
int
fbuf_recover(
	fbuf_t	*fb
) {
	int rc;

	rc = FBUF_RECOVER(fb);
	fb->saved = 0; /* ensure rewrite of restored data */
	fb->backuped = 1;
	return rc;
}

