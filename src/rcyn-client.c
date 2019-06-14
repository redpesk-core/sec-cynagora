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
#define CACHESIZE(x)  ((x) >= MIN_CACHE_SIZE ? (x) : (x) ? MIN_CACHE_SIZE : 0)

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

	/** spec of the socket */
	const char *socketspec;

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

	for (;;) {
		rc = prot_should_write(rcyn->prot);
		if (!rc)
			break;
		rc = prot_write(rcyn->prot, rcyn->fd);
		if (rc == -EAGAIN) {
			pfd.fd = rcyn->fd;
			pfd.events = POLLOUT;
			do { rc = poll(&pfd, 1, -1); } while (rc < 0 && errno == EINTR);
			if (rc < 0)
				rc = -errno;
		}
		if (rc < 0) {
			break;
		}
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
putxkv(
	rcyn_t *rcyn,
	const char *command,
	const char *optarg,
	const rcyn_key_t *optkey,
	const rcyn_value_t *optval
) {
	int rc, trial;
	prot_t *prot;
	char text[30];

	prot = rcyn->prot;
	for(trial = 0 ; ; trial++) {
		rc = prot_put_field(prot, command);
		if (!rc && optarg)
			rc = prot_put_field(prot, optarg);
		if (!rc && optkey) {
			rc = prot_put_field(prot, optkey->client);
			if (!rc)
				rc = prot_put_field(prot, optkey->session);
			if (!rc)
				rc = prot_put_field(prot, optkey->user);
			if (!rc)
				rc = prot_put_field(prot, optkey->permission);
		}
		if (!rc && optval) {
			rc = prot_put_field(prot, optval->value);
			if (!rc) {
				if (!optval->expire)
					text[0] = 0;
				else
					snprintf(text, sizeof text, "%lld", (long long)optval->expire);
				rc = prot_put_field(prot, text);
			}
		}
		if (!rc)
			rc = prot_put_end(prot);
		if (!rc) {
			/* client always flushes */
			rcyn->pending++;
			return flushw(rcyn);
		}
		prot_put_cancel(prot);
		if (trial >= 1)
			return rc;
		rc = flushw(rcyn);
		if (rc)
			return rc;
	}
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
	do { rc = poll(&pfd, 1, -1); } while (rc < 0 && errno == EINTR);
	return rc < 0 ? -errno : 0;
}

static
int
get_reply(
	rcyn_t *rcyn
) {
	;
	int rc;

	prot_next(rcyn->prot);
	rc = prot_get(rcyn->prot, &rcyn->reply.fields);
	if (rc > 0) {
		if (0 == strcmp(rcyn->reply.fields[0], _clear_)) {
			cache_clear(rcyn->cache,
				rc > 1 ? (uint32_t)atol(rcyn->reply.fields[1]) : 0);
			rc = 0;
		} else {
			if (0 != strcmp(rcyn->reply.fields[0], _item_))
				rcyn->pending--;
		}
	}
	rcyn->reply.count = rc;
	return rc;
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
	rcyn_t *rcyn,
	time_t *expire
) {
	int rc, exp;

	if (!strcmp(rcyn->reply.fields[0], _yes_)) {
		rc = 1;
		exp = 1;
	} else if (!strcmp(rcyn->reply.fields[0], _no_)) {
		rc = 0;
		exp = 1;
	} else if (!strcmp(rcyn->reply.fields[0], _done_)) {
		rc = -EEXIST;
		exp = 2;
	} else {
		rc = -EPROTO;
		exp = rcyn->reply.count;
	}
	if (exp < rcyn->reply.count)
		*expire = strtoll(rcyn->reply.fields[exp], NULL, 10);
	else
		*expire = 0;
	return rc;
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
	return rcyn->async.controlcb && rcyn->fd >= 0
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

	/* init the client */
	rcyn->pending = 0;
	rcyn->reply.count = -1;
	prot_reset(rcyn->prot);
	rcyn->fd = socket_open(rcyn->socketspec, 0);
	if (rcyn->fd < 0)
		return -errno;

	/* negociate the protocol */
	rc = putxkv(rcyn, _rcyn_, "1", 0, 0);
	if (rc >= 0) {
		rc = wait_pending_reply(rcyn);
		if (rc >= 0) {
			rc = -EPROTO;
			if (rcyn->reply.count >= 2
			 && 0 == strcmp(rcyn->reply.fields[0], _yes_)
			 && 0 == strcmp(rcyn->reply.fields[1], "1")) {
				cache_clear(rcyn->cache,
					rcyn->reply.count > 2 ? (uint32_t)atol(rcyn->reply.fields[2]) : 0);
				rc = async(rcyn, EPOLL_CTL_ADD, EPOLLIN);
				if (rc >= 0)
					return 0;
			}
		}
	}
	disconnection(rcyn);
	return rc;
}

static
int
ensure_opened(
	rcyn_t *rcyn
) {
	if (rcyn->fd >= 0 && write(rcyn->fd, NULL, 0) < 0)
		disconnection(rcyn);
	return rcyn->fd < 0 ? connection(rcyn) : 0;
}

/************************************************************************************/

int
rcyn_open(
	rcyn_t **prcyn,
	rcyn_type_t type,
	uint32_t cache_size,
	const char *socketspec
) {
	rcyn_t *rcyn;
	int rc;

	/* socket spec */
	switch(type) {
	default:
	case rcyn_Check: socketspec = rcyn_get_socket_check(socketspec); break;
	case rcyn_Admin: socketspec = rcyn_get_socket_admin(socketspec); break;
	case rcyn_Agent: socketspec = rcyn_get_socket_agent(socketspec); break;
	}

	/* allocate the structure */
	*prcyn = rcyn = malloc(sizeof *rcyn + 1 + strlen(socketspec));
	if (rcyn == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* create a protocol object */
	rc = prot_create(&rcyn->prot);
	if (rc < 0)
		goto error2;

	/* socket spec */
	strcpy((char*)(rcyn+1), socketspec);

	/* record type and weakly create cache */
	cache_create(&rcyn->cache, CACHESIZE(cache_size));
	rcyn->type = type;
	rcyn->socketspec = socketspec;
	rcyn->async.controlcb = NULL;
	rcyn->async.closure = 0;
	rcyn->async.requests = NULL;

	/* lazy connection */
	rcyn->fd = -1;

	/* done */
	return 0;

error2:
	free(rcyn);
error:
	*prcyn = NULL;
	return rc;
}

void
rcyn_disconnect(
	rcyn_t *rcyn
) {
	disconnection(rcyn);
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
	rc = ensure_opened(rcyn);
	if (rc < 0)
		return rc;

	rc = putxkv(rcyn, _enter_, 0, 0, 0);
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
	rc = ensure_opened(rcyn);
	if (rc < 0)
		return rc;

	rc = putxkv(rcyn, _leave_, commit ? _commit_ : 0/*default: rollback*/, 0, 0);
	if (rc >= 0)
		rc = wait_done(rcyn);
	return rc;
}

static
int
check_or_test(
	rcyn_t *rcyn,
	const rcyn_key_t *key,
	const char *action
) {
	int rc;
	time_t expire;

	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(rcyn);
	if (rc < 0)
		return rc;

	/* ensure there is no clear cache pending */
	flushr(rcyn);

	/* check cache item */
	rc = cache_search(rcyn->cache, key);
	if (rc >= 0)
		return rc;

	/* send the request */
	rc = putxkv(rcyn, action, 0, key, 0);
	if (rc >= 0) {
		/* get the response */
		rc = wait_pending_reply(rcyn);
		if (rc >= 0) {
			rc = status_check(rcyn, &expire);
			if (rcyn->cache && rc >= 0)
				cache_put(rcyn->cache, key, rc, expire);
		}
	}
	return rc;
}

int
rcyn_check(
	rcyn_t *rcyn,
	const rcyn_key_t *key
) {
	return check_or_test(rcyn, key, _check_);
}

int
rcyn_test(
	rcyn_t *rcyn,
	const rcyn_key_t *key
) {
	return check_or_test(rcyn, key, _test_);
}

int
rcyn_set(
	rcyn_t *rcyn,
	const rcyn_key_t *key,
	const rcyn_value_t *value
) {
	int rc;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(rcyn);
	if (rc < 0)
		return rc;

	rc = putxkv(rcyn, _set_, 0, key, value);
	if (rc >= 0)
		rc = wait_done(rcyn);
	return rc;
}

int
rcyn_get(
	rcyn_t *rcyn,
	const rcyn_key_t *key,
	void (*callback)(
		void *closure,
		const rcyn_key_t *key,
		const rcyn_value_t *value
	),
	void *closure
) {
	int rc;
	rcyn_key_t k;
	rcyn_value_t v;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(rcyn);
	if (rc < 0)
		return rc;

	rc = putxkv(rcyn, _get_, 0, key, 0);
	if (rc >= 0) {
		rc = wait_reply(rcyn, true);
		while ((rc == 6 || rc == 7) && !strcmp(rcyn->reply.fields[0], _item_)) {
			k.client = rcyn->reply.fields[1];
			k.session = rcyn->reply.fields[2];
			k.user = rcyn->reply.fields[3];
			k.permission = rcyn->reply.fields[4];
			v.value = rcyn->reply.fields[5];
			v.expire = rc == 6 ? 0 : (time_t)strtoll(rcyn->reply.fields[6], NULL, 10);
			callback(closure, &k, &v);
			rc = wait_reply(rcyn, true);
		}
		rc = status_done(rcyn);
	}
	return rc;
}

int
rcyn_log(
	rcyn_t *rcyn,
	int on,
	int off
) {
	int rc;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;

	rc = ensure_opened(rcyn);
	if (rc < 0)
		return rc;

	rc = putxkv(rcyn, _log_, off ? _off_ : on ? _on_ : 0, 0, 0);
	if (rc >= 0)
		rc = wait_done(rcyn);

	return rc < 0 ? rc : rcyn->reply.count < 2 ? 0 : !strcmp(rcyn->reply.fields[1], _on_);
}


int
rcyn_drop(
	rcyn_t *rcyn,
	const rcyn_key_t *key
) {
	int rc;

	if (rcyn->type != rcyn_Admin)
		return -EPERM;
	if (rcyn->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(rcyn);
	if (rc < 0)
		return rc;

	rc = putxkv(rcyn, _drop_, 0, key, 0);
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
	return cache_resize(&rcyn->cache, CACHESIZE(size));
}

void
rcyn_cache_clear(
	rcyn_t *rcyn
) {
	cache_clear(rcyn->cache, 0);
}

int
rcyn_cache_check(
	rcyn_t *rcyn,
	const rcyn_key_t *key
) {
	return cache_search(rcyn->cache, key);
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
	time_t expire;
	rcyn_key_t key;

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
		rc = status_check(rcyn, &expire);
		if (rc >= 0) {
			key.client = (const char*)(ar + 1);
			key.session = &key.client[1 + strlen(key.client)];
			key.user = &key.session[1 + strlen(key.session)];
			key.permission = &key.user[1 + strlen(key.user)];
			cache_put(rcyn->cache, &key, rc, expire);
		}
		ar->callback(ar->closure, rc);
		free(ar);
	}
}

int
rcyn_async_check(
	rcyn_t *rcyn,
	const rcyn_key_t *key,
	bool simple,
	void (*callback)(
		void *closure,
		int status),
	void *closure
) {
	int rc;
	asreq_t **pr, *ar;

	rc = ensure_opened(rcyn);
	if (rc < 0)
		return rc;

	/* allocate */
	ar = malloc(sizeof *ar + strlen(key->client) + strlen(key->session) + strlen(key->user) + strlen(key->permission) + 4);
	if (ar == NULL)
		return -ENOMEM;

	/* init */
	ar->next = NULL;
	ar->callback = callback;
	ar->closure = closure;
	stpcpy(1 + stpcpy(1 + stpcpy(1 + stpcpy((char*)(ar + 1), key->client), key->session), key->user), key->permission);

	/* send the request */
	rc = putxkv(rcyn, simple ? _test_ : _check_, 0, key, 0);
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

