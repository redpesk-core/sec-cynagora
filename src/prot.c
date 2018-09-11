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

#include <stdlib.h>
#include <limits.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>

#include "prot.h"

/** the structure buf is generic the meaning of pos/count is not fixed */
struct buf
{
	/** a position */
	unsigned pos;

	/** a count */
	unsigned count;

	/** a fixed size content */
	char content[MAXBUFLEN];
};
typedef struct buf buf_t;

/** structure for recording received fields */
struct fields
{
	/** count of field (negative if invalid) */
	int count;

	/** the fields as strings */
	const char *fields[MAXARGS];
};
typedef struct fields fields_t;

/** structure for handling the protocol */
struct prot
{
	/** input buf, pos is the scanning position */
	buf_t inbuf;

	/** output buf, pos is to be written position */
	buf_t outbuf;

	/** the fields */
	fields_t fields;
};


/**
 * Put the 'count' in 'fields' to the 'buf'
 * returns:
 *  - 0 on success
 *  - -EINVAL if the count of fields is too big
 *  - -ECANCELED if there is not enought space in the buffer
 */
static
int
buf_put_fields(
	buf_t *buf,
	unsigned count,
	const char **fields
) {
	unsigned ifield, pos, remain;
	const char *t;
	char c;

	/* check the count of fields */
	if (count > MAXARGS)
		return -EINVAL;

	/* get the writing position and the free count */
	pos = buf->pos + buf->count;
	if (pos >= MAXBUFLEN)
		pos -= MAXBUFLEN;
	remain = MAXBUFLEN - buf->count;

	/* put all fields */
	for (ifield = 0 ; ifield < count ; ifield++) {
		/* prepend the field separator if needed */
		if (ifield) {
			if (!remain--)
				goto cancel;
			buf->content[pos++] = FS;
			if (pos == MAXBUFLEN)
				pos = 0;
		}
		/* put the field if any (NULL aliases "") */
		t = fields[ifield];
		if (t) {
			/* put all chars of the field */
			while((c = *t++)) {
				/* escape special characters */
				if (c == FS || c == RS || c == ESC) {
					if (!remain--)
						goto cancel;
					buf->content[pos++] = ESC;
					if (pos == MAXBUFLEN)
						pos = 0;
				}
				/* put the char */
				if (!remain--)
					goto cancel;
				buf->content[pos++] = c;
				if (pos == MAXBUFLEN)
					pos = 0;
			}
		}
	}

	/* put the end indicator */
	if (!remain--)
		goto cancel;
	buf->content[pos] = RS;

	/* record the new values */
	buf->count = MAXBUFLEN - remain;
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
	if (buf->pos + count <= MAXBUFLEN) {
		vec[0].iov_len = count;
		n = 1;
	} else {
		vec[0].iov_len = MAXBUFLEN - buf->pos;
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
		if (buf->pos >= MAXBUFLEN)
			buf->pos -= MAXBUFLEN;
	}

	return (int)rc;
}

/* get the 'fields' from 'buf' */
static
void
buf_get_fields(
	buf_t *buf,
	fields_t *fields
) {
	char c;
	unsigned read, write;

	/* advance the pos after the end */
	assert(buf->content[buf->pos] == RS);
	buf->pos++;

	/* init first field */
	fields->fields[fields->count = 0] = buf->content;
	read = write = 0;
	for (;;) {
		c = buf->content[read++];
		switch(c) {
		case FS: /* field separator */
			buf->content[write++] = 0;
			if (fields->count >= MAXARGS)
				return;
			fields->fields[++fields->count] = &buf->content[write];
			break;
		case RS: /* end of line (record separator) */
			buf->content[write] = 0;
			fields->count += (write > 0);
			return;
		case ESC: /* escaping */
			c = buf->content[read++];
			if (c != FS && c != RS && c != ESC)
				buf->content[write++] = ESC;
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
		if (buf->content[buf->pos] == RS) {
			/* check whether RS is escaped */
			nesc = 0;
			while (buf->pos > nesc && buf->content[buf->pos - (nesc + 1)] == ESC)
				nesc++;
			if ((nesc & 1) == 0)
				return 1; /* not escaped */
		}
		buf->pos++;
	}
	return 0;
}

/** remove chars of 'buf' until pos */
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

/** read input 'buf' from 'fd' */
static
int
inbuf_read(
	buf_t *buf,
	int fd
) {
	ssize_t szr;
	int rc;

	if (buf->count == MAXBUFLEN)
		return -ENOBUFS;

	do {
		szr = read(fd, buf->content + buf->count, MAXBUFLEN - buf->count);
	} while(szr < 0 && errno == EINTR);
	if (szr >= 0)
		buf->count += (unsigned)(rc = (int)szr);
	else if (szr < 0)
		rc = -(errno == EWOULDBLOCK ? EAGAIN : errno);

	return rc;
}

/**
 * create the prot structure  in 'prot'
 * Return 0 in case of success or -ENOMEM in case of error
 */
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

/**
 * Destroys the protocol 'prot'
 */
void
prot_destroy(
	prot_t *prot
) {
	free(prot);
}

/**
 * reset the protocol 'prot'
 */
void
prot_reset(
	prot_t *prot
) {
	/* initialisation of the structure */
	prot->inbuf.pos = prot->inbuf.count = 0;
	prot->outbuf.pos = prot->outbuf.count = 0;
	prot->fields.count = -1;
}

/**
 * Put protocol encoded 'count' 'fields' to the output buffer
 * returns:
 *  - 0 on success
 *  - -EINVAL if the count of fields is too big
 *  - -ECANCELED if there is not enought space in the buffer
 */
int
prot_put(
	prot_t *prot,
	unsigned count,
	const char **fields
) {
	return buf_put_fields(&prot->outbuf, count, fields);
}

/**
 * Put protocol encoded fields until NULL found to the output buffer
 * returns:
 *  - 0 on success
 *  - -EINVAL if the count of fields is too big
 *  - -ECANCELED if there is not enought space in the buffer
 */
int
prot_putx(
	prot_t *prot,
	...
) {
	const char *p, *fields[MAXARGS];
	unsigned n;
	va_list l;

	va_start(l, prot);
	n = 0;
	p = va_arg(l, const char *);
	while (p) {
		if (n == MAXARGS)
			return -EINVAL;
		fields[n++] = p;
		p = va_arg(l, const char *);
	}
	va_end(l);
	return prot_put(prot, n, fields);
}

/**
 * Check whether write should be done or not
 * Returns 1 if there is something to write or 0 otherwise
 */
int
prot_should_write(
	prot_t *prot
) {
	return prot->outbuf.count > 0;
}

/**
 * Write the content to write and return either the count
 * of bytes written or an error code (negative). Note that
 * the returned value tries to be the same as those returned
 * by "man 2 write". The only exception is -ENODATA that is
 * returned if there is nothing to be written.
 */
int
prot_write(
	prot_t *prot,
	int fdout
) {
	return buf_write(&prot->outbuf, fdout);
}

int
prot_can_read(
	prot_t *prot
) {
	return prot->inbuf.count < MAXBUFLEN;
}

int
prot_read(
	prot_t *prot,
	int fdin
) {
	return inbuf_read(&prot->inbuf, fdin);
}

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

void
prot_next(
	prot_t *prot
) {
	if (prot->fields.count >= 0) {
		buf_crop(&prot->inbuf);
		prot->fields.count = -1;
	}
}


