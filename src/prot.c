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
/* IMPLEMENTATION OF THE PROTOCOL                                             */
/******************************************************************************/
/******************************************************************************/

#include "prot.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include <limits.h>

#ifndef PROT_MAX_FIELDS
#define PROT_MAX_FIELDS 20
#endif
#ifndef PROT_MAX_BUFFER_LENGTH
#define PROT_MAX_BUFFER_LENGTH 2000
#endif
#ifndef PROT_FIELD_SEPARATOR
#define PROT_FIELD_SEPARATOR ' '
#endif
#ifndef PROT_RECORD_SEPARATOR
#define PROT_RECORD_SEPARATOR '\n'
#endif
#ifndef PROT_ESCAPE
#define PROT_ESCAPE '\\'
#endif

/**
 * the structure buf is generic the meaning of pos/count is not fixed
 */
struct buf {
	/** a position */
	unsigned pos;

	/** a count */
	unsigned count;

	/** a fixed size content */
	char content[PROT_MAX_BUFFER_LENGTH];
};
typedef struct buf buf_t;

/** structure for recording received fields */
struct fields {
	/** count of field (negative if invalid) */
	int count;

	/** the fields as strings */
	const char *fields[PROT_MAX_FIELDS];
};
typedef struct fields fields_t;

/**
 * structure for handling the protocol
 */
struct prot {
	/**
	* input buf, pos is the scanning position, count is the count of bytes read
	*  +-------------------------------------+
	*  |<--decoded-->|<--pending-->|         |
	*  +-------------------------------------+
	*                ^pos          ^count
	*/
	buf_t inbuf;

	/**
	* output buf, pos is the start position, count is the count of byte to be written
	*  +-------------------------------------+
	*  |       |<---count--->|               |
	*  +-------------------------------------+
	*          ^pos
	*/
	buf_t outbuf;

	/** count of pending output fields */
	unsigned outfields;

	/** count of writable bytes */
	unsigned wrokcnt;

	/** allow empty records */
	int allow_empty;

	/** the fields */
	fields_t fields;
};

/**
 * Put the 'car' into the 'buf'
 * returns:
 *  - 0 on success
 *  - -ECANCELED if there is not enought space in the buffer
 */
static int buf_put_car(buf_t *buf, char car)
{
	unsigned pos;

	pos = buf->count;
	if (pos >= PROT_MAX_BUFFER_LENGTH)
		return -ECANCELED;

	buf->count = pos + 1;
	pos += buf->pos;
	if (pos >= PROT_MAX_BUFFER_LENGTH)
		pos -= PROT_MAX_BUFFER_LENGTH;
	buf->content[pos] = car;
	return 0;
}

/**
 * Put the 'string' into the 'buf' escaping it at need
 * returns:
 *  - 0 on success
 *  - -ECANCELED if there is not enought space in the buffer
 */
static int buf_put_string(buf_t *buf, const char *string)
{
	unsigned pos, remain, escape = 0;
	char c;

	remain = buf->count;
	pos = buf->pos + remain;
	if (pos >= PROT_MAX_BUFFER_LENGTH)
		pos -= PROT_MAX_BUFFER_LENGTH;
	remain = PROT_MAX_BUFFER_LENGTH - remain;

	/* put all chars of the string */
	while ((c = *string++)) {
		/* escape special characters */
		if (c == PROT_FIELD_SEPARATOR || c == PROT_RECORD_SEPARATOR)
			escape = 1;
		else if (c == PROT_ESCAPE)
			escape = *string == 0
			    || *string == PROT_FIELD_SEPARATOR
			    || *string == PROT_RECORD_SEPARATOR || *string == PROT_ESCAPE;
		if (escape) {
			if (!remain--)
				goto cancel;
			buf->content[pos++] = PROT_ESCAPE;
			if (pos == PROT_MAX_BUFFER_LENGTH)
				pos = 0;
			escape = 0;
		}
		/* put the char */
		if (!remain--)
			goto cancel;
		buf->content[pos++] = c;
		if (pos == PROT_MAX_BUFFER_LENGTH)
			pos = 0;
	}

	/* record the new values */
	buf->count = PROT_MAX_BUFFER_LENGTH - remain;
	return 0;

 cancel:
	return -ECANCELED;
}

/**
 * write part of the content of 'buf' to 'fd'
 */
static int buf_write_length(buf_t *buf, int fd, unsigned count)
{
	int n;
	ssize_t rc;
	struct iovec vec[2];

	/* get the count of byte to write (avoid int overflow) */
	if (count > buf->count)
		count = buf->count;
	if (count > INT_MAX)
		count = INT_MAX;

	/* calling it with nothing to write is an error */
	if (count == 0)
		return -ENODATA;

	/* prepare the iovec */
	vec[0].iov_base = buf->content + buf->pos;
	if (buf->pos + count <= PROT_MAX_BUFFER_LENGTH) {
		vec[0].iov_len = count;
		n = 1;
	} else {
		vec[0].iov_len = PROT_MAX_BUFFER_LENGTH - buf->pos;
		vec[1].iov_base = buf->content;
		vec[1].iov_len = count - vec[0].iov_len;
		n = 2;
	}

	/* write the buffers */
	do {
		rc = writev(fd, vec, n);
	} while (rc < 0 && errno == EINTR);

	/* check error */
	if (rc < 0)
		rc = -errno;
	else {
		/* update the state */
		buf->count -= (unsigned)rc;
		buf->pos += (unsigned)rc;
		if (buf->pos >= PROT_MAX_BUFFER_LENGTH)
			buf->pos -= PROT_MAX_BUFFER_LENGTH;
	}

	return (int)rc;
}

/**
 * get the 'fields' from 'buf'
 */
static void buf_get_fields(buf_t *buf, fields_t *fields)
{
	char c;
	unsigned read, write;

	/* advance the pos after the end */
	assert(buf->content[buf->pos] == PROT_RECORD_SEPARATOR);
	buf->pos++;

	/* init first field */
	fields->count = 0;
	read = write = 0;
	fields->fields[0] = buf->content;
	for (;;) {
		c = buf->content[read++];
		switch (c) {
		case PROT_FIELD_SEPARATOR:	/* field separator */
			buf->content[write++] = 0;
			if (fields->count >= PROT_MAX_FIELDS)
				return;
			if (++fields->count < PROT_MAX_FIELDS)
				fields->fields[fields->count] = &buf->content[write];
			break;
		case PROT_RECORD_SEPARATOR:	/* end of line (record separator) */
			buf->content[write] = 0;
			fields->count += (write > 0 && fields->count < PROT_MAX_FIELDS);
			return;
		case PROT_ESCAPE:	/* escaping */
			c = buf->content[read++];
			if (c != PROT_FIELD_SEPARATOR && c != PROT_RECORD_SEPARATOR && c != PROT_ESCAPE)
				buf->content[write++] = PROT_ESCAPE;
			buf->content[write++] = c;
			break;
		default:	/* other characters */
			buf->content[write++] = c;
			break;
		}
	}
}

/**
 * Advance pos of 'buf' until end of record RS found in buffer.
 * return 1 if found or 0 if not found
 */
static int buf_scan_end_record(buf_t *buf)
{
	unsigned nesc;

	/* search the next RS */
	while (buf->pos < buf->count) {
		if (buf->content[buf->pos] == PROT_RECORD_SEPARATOR) {
			/* check whether RS is escaped */
			nesc = 0;
			while (buf->pos > nesc && buf->content[buf->pos - (nesc + 1)] == PROT_ESCAPE)
				nesc++;
			if ((nesc & 1) == 0)
				return 1;	/* not escaped */
		}
		buf->pos++;
	}
	return 0;
}

/**
 * remove chars of 'buf' until pos
 */
static void buf_crop(buf_t *buf)
{
	buf->count -= buf->pos;
	if (buf->count)
		memmove(buf->content, buf->content + buf->pos, buf->count);
	buf->pos = 0;
}

/**
 * read input 'buf' from 'fd'
 */
static int inbuf_read(buf_t *buf, int fd)
{
	ssize_t szr;
	int rc;

	if (buf->count == PROT_MAX_BUFFER_LENGTH)
		return -ENOBUFS;

	do {
		szr = read(fd, buf->content + buf->count, PROT_MAX_BUFFER_LENGTH - buf->count);
	} while (szr < 0 && errno == EINTR);
	if (szr < 0)
		rc = -(errno == EWOULDBLOCK ? EAGAIN : errno);
	else {
		rc = (int)szr;
		buf->count += (unsigned)rc;
	}

	return rc;
}

/* see prot.h */
int prot_create(prot_t **prot)
{
	prot_t *p;

	/* allocation of the structure */
	*prot = p = malloc(sizeof *p);
	if (p == NULL)
		return -ENOMEM;

	/* initialisation of the structure */
	prot_reset(p);

	/* terminate */
	return 0;
}

/* see prot.h */
void prot_destroy(prot_t *prot)
{
	free(prot);
}

/* see prot.h */
void prot_reset(prot_t *prot)
{
	/* initialisation of the structure */
	prot->inbuf.pos = prot->inbuf.count = 0;
	prot->outbuf.pos = prot->outbuf.count = 0;
	prot->outfields = prot->wrokcnt = 0;
	prot->fields.count = -1;
	prot->allow_empty = 0;
}

/* see prot.h */
int prot_is_empty_allowed(prot_t *prot)
{
	return prot->allow_empty;
}

/* see prot.h */
void prot_set_allow_empty(prot_t *prot, int value)
{
	prot->allow_empty = !!value;
}

/* see prot.h */
void prot_put_cancel(prot_t *prot)
{
	if (prot->outfields) {
		prot->outbuf.count = prot->wrokcnt;
		prot->outfields = 0;
	}
}

/* see prot.h */
int prot_put_end(prot_t *prot)
{
	int rc = 0;

	if (prot->outfields || prot->allow_empty) {
		rc = buf_put_car(&prot->outbuf, PROT_RECORD_SEPARATOR);
		if (rc == 0) {
			prot->wrokcnt = prot->outbuf.count;
			prot->outfields = 0;
		}
	}
	return rc;
}

/* see prot.h */
int prot_put_field(prot_t *prot, const char *field)
{
	int rc = 0;

	if (prot->outfields++)
		rc = buf_put_car(&prot->outbuf, PROT_FIELD_SEPARATOR);
	if (rc >= 0 && field)
		rc = buf_put_string(&prot->outbuf, field);

	return rc;
}

/* see prot.h */
int prot_put_fields(prot_t *prot, unsigned count, const char **fields)
{
	int rc;
	if (!count)
		rc = 0;
	else {
		rc = prot_put_field(prot, *fields);
		while (rc >= 0 && --count)
			rc = prot_put_field(prot, *++fields);
	}
	return rc;
}

/* see prot.h */
int prot_put(prot_t *prot, unsigned count, const char **fields)
{
	int rc;

	rc = prot_put_fields(prot, count, fields);
	if (!rc)
		rc = prot_put_end(prot);
	if (rc)
		prot_put_cancel(prot);
	return rc;
}

/* see prot.h */
int prot_putx(prot_t *prot, ...)
{
	int rc = 0;
	va_list l;
	const char *p;

	va_start(l, prot);
	p = va_arg(l, const char *);
	while (!rc && p) {
		rc = prot_put_field(prot, p);
		p = va_arg(l, const char *);
	}
	va_end(l);
	if (rc)
		prot_put_cancel(prot);
	else
		rc = prot_put_end(prot);
	return rc;
}

/* see prot.h */
int prot_should_write(prot_t *prot)
{
	return prot->wrokcnt > 0;
}

/* see prot.h */
int prot_write(prot_t *prot, int fdout)
{
	int result = buf_write_length(&prot->outbuf, fdout, prot->wrokcnt);
	if (result > 0)
		prot->wrokcnt -= (unsigned)result;
	return result;
}

/* see prot.h */
int prot_can_read(prot_t *prot)
{
	return prot->inbuf.count < PROT_MAX_BUFFER_LENGTH;
}

/* see prot.h */
int prot_read(prot_t *prot, int fdin)
{
	return inbuf_read(&prot->inbuf, fdin);
}

/* see prot.h */
int prot_get(prot_t *prot, const char ***fields)
{
	for (;;) {
		if (prot->fields.count < 0) {
			if (!buf_scan_end_record(&prot->inbuf))
				return prot_can_read(prot) ? -EAGAIN : -EMSGSIZE;
			buf_get_fields(&prot->inbuf, &prot->fields);
		}
		if (prot->fields.count == 0 && !prot->allow_empty)
			prot_next(prot);
		else {
			if (fields)
				*fields = prot->fields.fields;
			return (int)prot->fields.count;
		}
	}
}

/* see prot.h */
void prot_next(prot_t *prot)
{
	if (prot->fields.count >= 0) {
		buf_crop(&prot->inbuf);
		prot->fields.count = -1;
	}
}
