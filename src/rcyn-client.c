#define _GNU_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/epoll.h>

#include "prot.h"
#include "rcyn-protocol.h"
#include "rcyn-client.h"
#include "cache.h"
#include "socket.h"

#define MIN_CACHE_SIZE 400

struct asreq;
typedef struct asreq asreq_t;

/** recording of asynchronous requests */
struct asreq
{
	/** link to the next pending request */
	asreq_t *next;

	/** callback function */
	void (*callback)(
		void *closure,
		int status);

	/** closure of the callback */
	void *closure;
};

struct rcyn;
typedef struct rcyn rcyn_t;

/**
 * structure recording a rcyn client
 */
struct rcyn
{
	/** file descriptor of the socket */
	int fd;

	/** count of pending requests */
	int pending;

	/** type of link */
	rcyn_type_t type;

	/** protocol manager object */
	prot_t *prot;

	/** cache  object */
	cache_t *cache;

	/** copy of the reply */
	struct {
		/** count of fields of the reply */
		int count;

		/** fields (or fields) of the reply */
		const char **fields;
	} reply;

	/** async */
	struct {
		/** control callback */
		rcyn_async_ctl_t controlcb;

		/** closure */
		void *closure;

		/** reqests */
		asreq_t *requests;
	} async;
};

static void disconnection(rcyn_t *rcyn);

/**
 * Flush the write buffer
 */
static
int
flushw(
	rcyn_t *rcyn
) {
	int rc;
	struct pollfd pfd;

	rc = prot_should_write(rcyn->prot);
	while (rc) {
		rc = prot_write(rcyn->prot, rcyn->fd);
		if (rc == -EAGAIN) {
			pfd.fd = rcyn->fd;
			pfd.events = POLLOUT;
			do { rc = poll(&pfd, 1, 0); } while (rc < 0 && errno == EINTR);
			if (rc < 0)
				rc = -errno;
		}
		if (rc < 0) {
			break;
		}
		rc = prot_should_write(rcyn->prot);
	}
	return rc;
}

/**
 * Put the command made of arguments ...
 * Increment the count of pending requests.
 * Return 0 in case of success or a negative number on error.
 */
static
int
putx(
	rcyn_t *rcyn,
	...
) {
	const char *p, *fields[MAXARGS];
	unsigned n;
	va_list l;
	int rc;

	/* reconstruct the array of arguments */
	va_start(l, rcyn);
	n = 0;
	p = va_arg(l, const char *);
	while (p && n < MAXARGS) {
		fields[n++] = p;
		p = va_arg(l, const char *);
	}
	va_end(l);

	/* put it to the output buffer */
	rc = prot_put(rcyn->prot, n, fields);
	if (rc == -ECANCELED) {
		/* not enough room in the buffer, flush it */
		rc = flushw(rcyn);
		if (rc == 0)
			rc = prot_put(rcyn->prot, n, fields);
	}
	/* client always flushes */
	if (rc == 0) {
		rcyn->pending++;
		rc = flushw(rcyn);
	}
	return rc;
}

static
int
wait_input(
	rcyn_t *rcyn
) {
	int rc;
	struct pollfd pfd;

	pfd.fd = rcyn->fd;
	pfd.events = POLLIN;
	do { rc = poll(&pfd, 1, 0); } while (rc < 0 && errno == EINTR);
	return rc < 0 ? -errno : 0;
}

static
int
get_reply(
	rcyn_t *rcyn
) {
	int rc;

	prot_next(rcyn->prot);
	rc = rcyn->reply.count = prot_get(rcyn->prot, &rcyn->reply.fields);
	if (rc <= 0)
		return rc;
	if (0 != strcmp(rcyn->reply.fields[0], _clear_)) {
		if (0 != strcmp(rcyn->reply.fields[0], _item_))
			rcyn->pending--;
		return rc;
	}
	cache_clear(rcyn->cache);
	return rcyn->reply.count = 0;
}

static
int
wait_reply(
	rcyn_t *rcyn,
	bool block
) {
	int rc;

	for(;;) {
		prot_next(rcyn->prot);
		rc = get_reply(rcyn);
		if (rc > 0)
			return rc;
		if (rc < 0) {
			rc = prot_read(rcyn->prot, rcyn->fd);
			while (rc <= 0) {
				if (rc == 0)
					return -(errno = EPIPE);
				if (rc == -EAGAIN && block)
					rc = wait_input(rcyn);
				if (rc < 0)
					return rc;
				rc = prot_read(rcyn->prot, rcyn->fd);
			}
		}
	}
}

static
int
flushr(
	rcyn_t *rcyn
) {
	int rc;

	do { rc = wait_reply(rcyn, false); } while(rc > 0);
	return rc;
}

static
int
status_done(
	rcyn_t *rcyn
) {
	return strcmp(rcyn->reply.fields[0], _done_) ? -ECANCELED : 0;
}

static
int
status_check(
	rcyn_t *rcyn
) {
	return !strcmp(rcyn->reply.fields[0], _yes_) ? 1
		: !strcmp(rcyn->reply.fields[0], _no_) ? 0
		: -EEXIST;
}

static
int
wait_pending_reply(
	rcyn_t *rcyn
) {
	int rc;
	for (;;) {
		rc = wait_reply(rcyn, true);
		if (rc < 0)
			return rc;
		if (rc > 0 && rcyn->pending == 0)
			return rc;
	}
}

static
int
wait_done(
	rcyn_t *rcyn
) {
	int rc = wait_pending_reply(rcyn);
	if (rc > 0)
		rc = status_done(rcyn);
	return rc;
}

static
int
async(
	rcyn_t *rcyn,
	int op,
	uint32_t events
) {
	return rcyn->async.controlcb
		? rcyn->async.controlcb(rcyn->async.closure, op, rcyn->fd, events)
		: 0;
}

static
void
disconnection(
	rcyn_t *rcyn
) {
	if (rcyn->fd >= 0) {
		async(rcyn, EPOLL_CTL_DEL, 0);
		close(rcyn->fd);
		rcyn->fd = -1;
	}
}

static
int
connection(
	rcyn_t *rcyn
) {
	int rc;
	const char *spec;

	/* socket spec */
	switch(rcyn->type) {
	default:
	case rcyn_Check: spec = rcyn_default_check_socket_spec; break;
	case rcyn_Admin: spec = rcyn_default_admin_socket_spec; break;
	}

	/* init the client */
	rcyn->pending = 0;
	rcyn->reply.count = -1;
	cache_clear(rcyn->cache);
	prot_reset(rcyn->prot);
	rcyn->fd = socket_open(spec, 0);
	if (rcyn->fd < 0)
		return -errno;

	/* negociate the protocol */
	rc = putx(rcyn, _rcyn_, "1", NULL);
	if (rc >= 0) {
		rc = wait_pending_reply(rcyn);
		if (rc >= 0) {
			rc = -EPROTO;
			if (rcyn->reply.count == 2
			 && 0 == strcmp(rcyn->reply.fields[0], _yes_)
			 && 0 == strcmp(rcyn->reply.fields[1], "1")) {
				rc = async(rcyn, EPOLL_CTL_ADD, EPOLLIN);
				if (rc >= 0)
					return 0;
			}
		}
	}
	disconnection(rcyn);
	return rc;
}


/************************************************************************************/

int
rcyn_open(
	rcyn_t **prcyn,
	rcyn_type_t type,
	uint32_t cache_size
) {
	rcyn_t *rcyn;
	int rc;

	/* allocate the structure */
	*prcyn = rcyn = malloc(sizeof *rcyn);
	if (rcyn == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* create a protocol object */
	rc = prot_create(&rcyn->prot);
	if (rc < 0)
		goto error2;

	/* record type and weakly create cache */
	cache_create(&rcyn->cache, cache_size < MIN_CACHE_SIZE ? MIN_CACHE_SIZE : cache_size);
	rcyn->type = type;
	rcyn->async.controlcb = NULL;
	rcyn->async.closure = 0;
	rcyn->async.requests = NULL;

	/* connection */
	rc = connection(rcyn);
	if (rc < 0)
		goto error3;

	/* done */
	return 0;

error3:
	free(rcyn->cache);
	prot_destroy(rcyn->prot);
error2:
	free(rcyn);
error:
	*prcyn = NULL;
	return rc;
}

void
rcyn_close(
	rcyn_t *rcyn
) {
	rcyn_async_setup(rcyn, NULL, NULL);
	disconnection(rcyn);
	prot_destroy(rcyn->prot);
	free(rcyn->cache);
	free(rcyn);
}

int
rcyn_enter(
	rcyn_t *rcyn
) {
	int rc;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;

	rc = putx(rcyn, _enter_, NULL);
	if (rc >= 0)
		rc = wait_done(rcyn);
	return rc;
}

int
rcyn_leave(
	rcyn_t *rcyn,
	bool commit
) {
	int rc;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;

	rc = putx(rcyn, _leave_, commit ? _commit_ : NULL/*default: rollback*/, NULL);
	if (rc >= 0)
		rc = wait_done(rcyn);
	return rc;
}

static
int
check_or_test(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char *action
) {
	int rc;

	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;

	/* ensure there is no clear cache pending */
	flushr(rcyn);

	/* check cache item */
	rc = cache_search(rcyn->cache, client, session, user, permission);
	if (rc >= 0)
		return rc;

	/* send the request */
	rc = putx(rcyn, action, client, session, user, permission, NULL);
	if (rc >= 0) {
		/* get the response */
		rc = wait_pending_reply(rcyn);
		if (rc >= 0) {
			rc = status_check(rcyn);
			if (rc >= 0)
				cache_put(rcyn->cache, client, session, user, permission, rc);
		}
	}
	return rc;
}

int
rcyn_check(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	return check_or_test(rcyn, client, session, user, permission, _check_);
}

int
rcyn_test(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	return check_or_test(rcyn, client, session, user, permission, _check_);
}

int
rcyn_set(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	int value
) {
	char val[30];
	int rc;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;

	snprintf(val, sizeof val, "%u", (unsigned)value);
	rc = putx(rcyn, _set_, client, session, user, permission, val, NULL);
	if (rc >= 0)
		rc = wait_done(rcyn);
	return rc;
}

int
rcyn_get(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	void (*callback)(
		void *closure,
		const char *client,
		const char *session,
		const char *user,
		const char *permission,
		uint32_t value
	),
	void *closure
) {
	int rc;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;

	rc = putx(rcyn, _get_, client, session, user, permission, NULL);
	if (rc >= 0) {
		rc = wait_reply(rcyn, true);
		while (rc == 6 && !strcmp(rcyn->reply.fields[0], _item_)) {
			callback(closure,
				rcyn->reply.fields[1],
				rcyn->reply.fields[2],
				rcyn->reply.fields[3],
				rcyn->reply.fields[4],
				(uint32_t)atoi(rcyn->reply.fields[5]));
			rc = wait_reply(rcyn, true);
		}
		rc = status_done(rcyn);
	}
	return rc;
}

int
rcyn_drop(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	int rc;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;

	rc = putx(rcyn, _drop_, client, session, user, permission, NULL);
	if (rc >= 0)
		rc = wait_done(rcyn);
	return rc;
}

/************************************************************************************/

int
rcyn_cache_resize(
	rcyn_t *rcyn,
	uint32_t size
) {
	return cache_resize(&rcyn->cache, size < MIN_CACHE_SIZE ? MIN_CACHE_SIZE : size);
}

void
rcyn_cache_clear(
	rcyn_t *rcyn
) {
	cache_clear(rcyn->cache);
}

int
rcyn_cache_check(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	return cache_search(rcyn->cache, client, session, user, permission);
}


/************************************************************************************/

int
rcyn_async_setup(
	rcyn_t *rcyn,
	rcyn_async_ctl_t controlcb,
	void *closure
) {
	asreq_t *ar;

	/* cancel pending requests */
	while((ar = rcyn->async.requests) != NULL) {
		rcyn->async.requests = ar->next;
		ar->callback(ar->closure, -ECANCELED);
		free(ar);
	}
	/* remove existing polling */
	async(rcyn, EPOLL_CTL_DEL, 0);
	/* records new data */
	rcyn->async.controlcb = controlcb;
	rcyn->async.closure = closure;
	/* record to polling */
	return async(rcyn, EPOLL_CTL_ADD, EPOLLIN);
}

int
rcyn_async_process(
	rcyn_t *rcyn
) {
	int rc;
	const char *first;
	asreq_t *ar;

	for (;;) {
		/* non blocking wait for a reply */
		rc = wait_reply(rcyn, false);
		if (rc < 0)
			return rc == -EAGAIN ? 0 : rc;

		/* skip empty replies */
		if (rc == 0)
			continue;

		/* skip done/error replies */
		first = rcyn->reply.fields[0];
		if (!strcmp(first, _done_)
		 || !strcmp(first, _error_))
			continue;

		/* ignore unexpected answers */
		ar = rcyn->async.requests;
		if (ar == NULL)
			continue;

		/* emit the asynchronous answer */
		rcyn->async.requests = ar->next;
		rc = status_check(rcyn);
		ar->callback(ar->closure, rc);
		free(ar);
	}
}

int
rcyn_async_check(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	bool simple,
	void (*callback)(
		void *closure,
		int status),
	void *closure
) {
	int rc;
	asreq_t **pr, *ar;

	/* allocate */
	ar = malloc(sizeof *ar);
	if (ar == NULL)
		return -ENOMEM;

	/* init */
	ar->next = NULL;
	ar->callback = callback;
	ar->closure = closure;

	/* send the request */
	rc = putx(rcyn, simple ? _test_ : _check_,
		client, session, user, permission, NULL);
	if (rc >= 0)
		rc = flushw(rcyn);
	if (rc < 0) {
		free(ar);
		return rc;
	}

	/* record the request */
	pr = &rcyn->async.requests;
	while(*pr != NULL)
		pr = &(*pr)->next;
	*pr = ar;
	return 0;
}


