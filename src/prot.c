/*
 * Copyright (C) 2018-2022 IoT.bzh Company
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

#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

#include "prot.h"

#define MAX_FIELDS          20
#define MAX_BUFFER_LENGTH   2000
#define FIELD_SEPARATOR     ' '
#define RECORD_SEPARATOR    '\n'
#define ESCAPE              '\\'

/**
 * the structure buf is generic the meaning of pos/count is not fixed
 */
struct buf
{
	/** a position */
	unsigned pos;

	/** a count */
	unsigned count;

	/* TODO: add a 3rd unsigned for improving management of read and write */

	/** a fixed size content */
	char content[MAX_BUFFER_LENGTH];
};
typedef struct buf buf_t;

/** structure for recording received fields */
struct fields
{
	/** count of field (negative if invalid) */
	int count;

	/** the fields as strings */
	const char *fields[MAX_FIELDS];
};
typedef struct fields fields_t;

/**
 * structure for handling the protocol
 */
struct prot
{
	/** input buf, pos is the scanning position */
	buf_t inbuf;

	/** output buf, pos is to be written position */
	buf_t outbuf;

	/** count of pending output fields */
	unsigned outfields;

	/** cancel index when putting values */
	unsigned cancelidx;

	/** the fields */
	fields_t fields;
};

/**
 * Put the 'car' into the 'buf'
 * returns:
 *  - 0 on success
 *  - -ECANCELED if there is not enought space in the buffer
 */
static
int
buf_put_car(
	buf_t *buf,
	char car
) {
	unsigned pos;

	pos = buf->count;
	if (pos >= MAX_BUFFER_LENGTH)
		return -ECANCELED;

	buf->count = pos + 1;
	pos += buf->pos;
	if (pos >= MAX_BUFFER_LENGTH)
		pos -= MAX_BUFFER_LENGTH;
	buf->content[pos] = car;
	return 0;
}

/**
 * Put the 'string' into the 'buf' escaping it at need
 * returns:
 *  - 0 on success
 *  - -ECANCELED if there is not enought space in the buffer
 */
static
int
buf_put_string(
	buf_t *buf,
	const char *string
) {
	unsigned pos, remain;
	char c;

	remain = buf->count;
	pos = buf->pos + remain;
	if (pos >= MAX_BUFFER_LENGTH)
		pos -= MAX_BUFFER_LENGTH;
	remain = MAX_BUFFER_LENGTH - remain;

	/* put all chars of the string */
	while((c = *string++)) {
		/* escape special characters */
		if (c == FIELD_SEPARATOR || c == RECORD_SEPARATOR || c == ESCAPE) {
			if (!remain--)
				goto cancel;
			buf->content[pos++] = ESCAPE;
			if (pos == MAX_BUFFER_LENGTH)
				pos = 0;
		}
		/* put the char */
		if (!remain--)
			goto cancel;
		buf->content[pos++] = c;
		if (pos == MAX_BUFFER_LENGTH)
			pos = 0;
	}

	/* record the new values */
	buf->count = MAX_BUFFER_LENGTH - remain;
	return 0;

cancel:
	return -ECANCELED;
}

/**
 * write the content of 'buf' to 'fd'
 */
static
int
buf_write(
	buf_t *buf,
	int fd
) {
	int n;
	unsigned count;
	ssize_t rc;
	struct iovec vec[2];

	/* get the count of byte to write (avoid int overflow) */
	count = buf->count > INT_MAX ? INT_MAX : buf->count;

	/* calling it with nothing to write is an error */
	if (count == 0)
		return -ENODATA;

	/* prepare the iovec */
	vec[0].iov_base = buf->content + buf->pos;
	if (buf->pos + count <= MAX_BUFFER_LENGTH) {
		vec[0].iov_len = count;
		n = 1;
	} else {
		vec[0].iov_len = MAX_BUFFER_LENGTH - buf->pos;
		vec[1].iov_base = buf->content;
		vec[1].iov_len = count - vec[0].iov_len;
		n = 2;
	}

	/* write the buffers */
	do {
		rc = writev(fd, vec, n);
	} while(rc < 0 && errno == EINTR);

	/* check error */
	if (rc < 0)
		rc = -errno;
	else {
		/* update the state */
		buf->count -= (unsigned)rc;
		buf->pos += (unsigned)rc;
		if (buf->pos >= MAX_BUFFER_LENGTH)
			buf->pos -= MAX_BUFFER_LENGTH;
	}

	return (int)rc;
}

/**
 * get the 'fields' from 'buf'
 */
static
void
buf_get_fields(
	buf_t *buf,
	fields_t *fields
) {
	char c;
	unsigned read, write;

	/* advance the pos after the end */
	assert(buf->content[buf->pos] == RECORD_SEPARATOR);
	buf->pos++;

	/* init first field */
	fields->fields[fields->count = 0] = buf->content;
	read = write = 0;
	for (;;) {
		c = buf->content[read++];
		switch(c) {
		case FIELD_SEPARATOR: /* field separator */
			buf->content[write++] = 0;
			if (fields->count >= MAX_FIELDS)
				return;
			fields->fields[++fields->count] = &buf->content[write];
			break;
		case RECORD_SEPARATOR: /* end of line (record separator) */
			buf->content[write] = 0;
			fields->count += (write > 0);
			return;
		case ESCAPE: /* escaping */
			c = buf->content[read++];
			if (c != FIELD_SEPARATOR && c != RECORD_SEPARATOR && c != ESCAPE)
				buf->content[write++] = ESCAPE;
			buf->content[write++] = c;
			break;
		default: /* other characters */
			buf->content[write++] = c;
			break;
		}
	}
}

/**
 * Advance pos of 'buf' until end of record RS found in buffer.
 * return 1 if found or 0 if not found
 */
static
int
buf_scan_end_record(
	buf_t *buf
) {
	unsigned nesc;

	/* search the next RS */
	while(buf->pos < buf->count) {
		if (buf->content[buf->pos] == RECORD_SEPARATOR) {
			/* check whether RS is escaped */
			nesc = 0;
			while (buf->pos > nesc && buf->content[buf->pos - (nesc + 1)] == ESCAPE)
				nesc++;
			if ((nesc & 1) == 0)
				return 1; /* not escaped */
		}
		buf->pos++;
	}
	return 0;
}

/**
 * remove chars of 'buf' until pos
 */
static
void
buf_crop(
	buf_t *buf
) {
	buf->count -= buf->pos;
	if (buf->count)
		memmove(buf->content, buf->content + buf->pos, buf->count);
	buf->pos = 0;
}

/**
 * read input 'buf' from 'fd'
 */
static
int
inbuf_read(
	buf_t *buf,
	int fd
) {
	ssize_t szr;
	int rc;

	if (buf->count == MAX_BUFFER_LENGTH)
		return -ENOBUFS;

	do {
		szr = read(fd, buf->content + buf->count, MAX_BUFFER_LENGTH - buf->count);
	} while(szr < 0 && errno == EINTR);
	if (szr >= 0)
		buf->count += (unsigned)(rc = (int)szr);
	else if (szr < 0)
		rc = -(errno == EWOULDBLOCK ? EAGAIN : errno);

	return rc;
}

/* see prot.h */
int
prot_create(
	prot_t **prot
) {
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
void
prot_destroy(
	prot_t *prot
) {
	free(prot);
}

/* see prot.h */
void
prot_reset(
	prot_t *prot
) {
	/* initialisation of the structure */
	prot->inbuf.pos = prot->inbuf.count = 0;
	prot->outbuf.pos = prot->outbuf.count = 0;
	prot->outfields = 0;
	prot->fields.count = -1;
}

/* see prot.h */
void
prot_put_cancel(
	prot_t *prot
) {
	unsigned count;

	if (prot->outfields) {
		count = prot->cancelidx - prot->outbuf.pos;
		prot->outbuf.count = count > MAX_BUFFER_LENGTH ? count - MAX_BUFFER_LENGTH : count;
		prot->outfields = 0;
	}
}

/* see prot.h */
int
prot_put_end(
	prot_t *prot
) {
	int rc;

	if (!prot->outfields)
		rc = 0;
	else {
		rc = buf_put_car(&prot->outbuf, RECORD_SEPARATOR);
		if (rc == 0)
			prot->outfields = 0;
	}
	return rc;
}

/* see prot.h */
int
prot_put_field(
	prot_t *prot,
	const char *field
) {
	int rc;

	if (prot->outfields++)
		rc = buf_put_car(&prot->outbuf, FIELD_SEPARATOR);
	else {
		prot->cancelidx = prot->outbuf.pos + prot->outbuf.count;
		rc = 0;
	}
	if (rc >= 0 && field)
		rc = buf_put_string(&prot->outbuf, field);

	return rc;
}

/* see prot.h */
int
prot_put_fields(
	prot_t *prot,
	unsigned count,
	const char **fields
) {
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
int
prot_put(
	prot_t *prot,
	unsigned count,
	const char **fields
) {
	int rc;

	rc = prot_put_fields(prot, count, fields);
	if (!rc)
		rc = prot_put_end(prot);
	if (rc)
		prot_put_cancel(prot);
	return rc;
}

/**
 * Put protocol encoded fields until NULL found to the output buffer
 * returns:
 *  - 0 on success
 *  - -EINVAL if the count of fields is too big
 *  - -ECANCELED if there is not enought space in the buffer
 */
/* see prot.h */
int
prot_putx(
	prot_t *prot,
	...
) {
	int rc = 0;
	va_list l;
	const char *p;

	va_start(l, prot);
	p = va_arg(l, const char *);
	while(!rc && p) {
		rc = prot_put_field(prot, p);
		p = va_arg(l, const char *);
	}
	if (rc)
		prot_put_cancel(prot);
	else
		rc = prot_put_end(prot);
	va_end(l);
	return rc;
}

/* see prot.h */
int
prot_should_write(
	prot_t *prot
) {
	return prot->outbuf.count > 0;
}

/* see prot.h */
int
prot_write(
	prot_t *prot,
	int fdout
) {
	return buf_write(&prot->outbuf, fdout);
}

/* see prot.h */
int
prot_can_read(
	prot_t *prot
) {
	return prot->inbuf.count < MAX_BUFFER_LENGTH;
}

/* see prot.h */
int
prot_read(
	prot_t *prot,
	int fdin
) {
	return inbuf_read(&prot->inbuf, fdin);
}

/* see prot.h */
int
prot_get(
	prot_t *prot,
	const char ***fields
) {
	if (prot->fields.count < 0) {
		if (!buf_scan_end_record(&prot->inbuf))
			return -EAGAIN;
		buf_get_fields(&prot->inbuf, &prot->fields);
	}
	if (fields)
		*fields = prot->fields.fields;
	return (int)prot->fields.count;
}

/* see prot.h */
void
prot_next(
	prot_t *prot
) {
	if (prot->fields.count >= 0) {
		buf_crop(&prot->inbuf);
		prot->fields.count = -1;
	}
}


