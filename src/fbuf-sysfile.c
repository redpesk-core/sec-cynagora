/*
 * Copyright (C) 2024-2026 IoT.bzh Company
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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fbuf-sysfile.h"

/* see fbuf-sysfile.h */
int
fbuf_sysfile_read_file(
	fbuf_t *fb,
	const char *name
) {
	int fd, rc;
	uint32_t sz;
	struct stat st, stb;

	/* open the file */
	fd = open(name, O_RDWR|O_CREAT, 0600);
	if (fd < 0) {
		rc = -errno;
		goto error;
	}

	/* get file stat */
	rc = fstat(fd, &st);
	if (rc < 0) {
		rc = -errno;
		goto error2;
	}

	/* compute backuped flag */
	rc = stat(fb->backup, &stb);
	fb->backuped = rc == 0 && st.st_dev == stb.st_dev && st.st_ino == stb.st_ino;

	/* check file size */
	if ((off_t)INT32_MAX < st.st_size) {
		rc = -EFBIG;
		goto error2;
	}
	sz = (uint32_t)st.st_size;

	/* allocate memory */
	rc = fbuf_ensure_capacity(fb, (uint32_t)st.st_size);
	if (rc < 0)
		goto error2;

	/* read the file */
	if (read(fd, fb->buffer, (size_t)sz) != (ssize_t)sz) {
		rc = -errno;
		goto error2;
	}

	/* done */
	fb->used = fb->size = sz;
	close(fd);

	return 0;

error2:
	close(fd);
error:
	fprintf(stderr, "can't read file %s: %s\n", name, strerror(-rc));
	fb->saved = fb->used = fb->size = 0;
	return rc;
}

/* see fbuf_sysfile.h */
int fbuf_sysfile_write_file(fbuf_t *fb, const char *name)
{
	ssize_t rcs;
	int rc, fd;

	/* open the file */
	unlink(name);
	fd = creat(name, 0600);
	if (fd < 0) {
		rc = -errno;
		goto error;
	}

	/* write unsaved bytes */
	rcs = write(fd, fb->buffer, fb->used);
	close(fd);
	if (rcs < 0) {
		rc = -errno;
		goto error;
	}
	if ((uint32_t)rcs != fb->used) {
		rc = -EINTR;
		goto error;
	}

	return 0;

error:
	fprintf(stderr, "write of file %s failed: %s\n", name, strerror(-rc));
	return rc;
}

/* see fbuf-sysfile.h */
int fbuf_sysfile_read(fbuf_t *fb)
{
	return fbuf_sysfile_read_file(fb, fb->name);
}

/* see fbuf-sysfile.h */
int fbuf_sysfile_sync(fbuf_t *fb)
{
	return fbuf_sysfile_write_file(fb, fb->name);
}

/* see fbuf-sysfile.h */
int fbuf_sysfile_backup(fbuf_t *fb)
{
	unlink(fb->backup);
	return link(fb->name, fb->backup);
}

/* see fbuf-sysfile.h */
int fbuf_sysfile_recover(fbuf_t *fb)
{
	return fbuf_sysfile_read_file(fb, fb->backup);
}

