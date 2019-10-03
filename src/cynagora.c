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
/* IMPLEMENTATION OF CLIENT PART OF CYNAGORA-PROTOCOL                         */
/******************************************************************************/
/******************************************************************************/

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
#include "cyn-protocol.h"
#include "cynagora.h"
#include "cache.h"
#include "socket.h"

#define MIN_CACHE_SIZE 400
#define CACHESIZE(x)  ((x) >= MIN_CACHE_SIZE ? (x) : (x) ? MIN_CACHE_SIZE : 0)

/** recording of asynchronous requests */
struct asreq
{
	/** link to the next pending request */
	struct asreq *next;

	/** callback function */
	void (*callback)(
		void *closure,
		int status);

	/** closure of the callback */
	void *closure;
};
typedef struct asreq asreq_t;

/**
 * structure recording a client
 */
struct cynagora
{
	/** file descriptor of the socket */
	int fd;

	/** count of pending requests */
	int pending;

	/** type of link */
	cynagora_type_t type;

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
		cynagora_async_ctl_t controlcb;

		/** closure */
		void *closure;

		/** requests */
		asreq_t *requests;
	} async;
};

static void disconnection(cynagora_t *cynagora);

/**
 * Flush the write buffer
 */
static
int
flushw(
	cynagora_t *cynagora
) {
	int rc;
	struct pollfd pfd;

	for (;;) {
		rc = prot_should_write(cynagora->prot);
		if (!rc)
			break;
		rc = prot_write(cynagora->prot, cynagora->fd);
		if (rc == -EAGAIN) {
			pfd.fd = cynagora->fd;
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
	cynagora_t *cynagora,
	const char *command,
	const char *optarg,
	const cynagora_key_t *optkey,
	const cynagora_value_t *optval
) {
	int rc, trial;
	prot_t *prot;
	char text[30];

	prot = cynagora->prot;
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
			cynagora->pending++;
			return flushw(cynagora);
		}
		prot_put_cancel(prot);
		if (trial >= 1)
			return rc;
		rc = flushw(cynagora);
		if (rc)
			return rc;
	}
}

static
int
wait_input(
	cynagora_t *cynagora
) {
	int rc;
	struct pollfd pfd;

	pfd.fd = cynagora->fd;
	pfd.events = POLLIN;
	do { rc = poll(&pfd, 1, -1); } while (rc < 0 && errno == EINTR);
	return rc < 0 ? -errno : 0;
}

static
int
get_reply(
	cynagora_t *cynagora
) {
	int rc;

	prot_next(cynagora->prot);
	rc = prot_get(cynagora->prot, &cynagora->reply.fields);
	if (rc > 0) {
		if (0 == strcmp(cynagora->reply.fields[0], _clear_)) {
			cache_clear(cynagora->cache,
				rc > 1 ? (uint32_t)atol(cynagora->reply.fields[1]) : 0);
			rc = 0;
		} else {
			if (0 != strcmp(cynagora->reply.fields[0], _item_))
				cynagora->pending--;
		}
	}
	cynagora->reply.count = rc;
	return rc;
}

static
int
wait_reply(
	cynagora_t *cynagora,
	bool block
) {
	int rc;

	for(;;) {
		prot_next(cynagora->prot);
		rc = get_reply(cynagora);
		if (rc > 0)
			return rc;
		if (rc < 0) {
			rc = prot_read(cynagora->prot, cynagora->fd);
			while (rc <= 0) {
				if (rc == 0)
					return -(errno = EPIPE);
				if (rc == -EAGAIN && block)
					rc = wait_input(cynagora);
				if (rc < 0)
					return rc;
				rc = prot_read(cynagora->prot, cynagora->fd);
			}
		}
	}
}

static
int
flushr(
	cynagora_t *cynagora
) {
	int rc;

	do { rc = wait_reply(cynagora, false); } while(rc > 0);
	return rc;
}

static
int
status_done(
	cynagora_t *cynagora
) {
	return strcmp(cynagora->reply.fields[0], _done_) ? -ECANCELED : 0;
}

static
int
status_check(
	cynagora_t *cynagora,
	time_t *expire
) {
	int rc;

	if (!strcmp(cynagora->reply.fields[0], _yes_))
		rc = 1;
	else if (!strcmp(cynagora->reply.fields[0], _no_))
		rc = 0;
	else if (!strcmp(cynagora->reply.fields[0], _done_))
		rc = -EEXIST;
	else
		rc = -EPROTO;

	if (cynagora->reply.count >= 2)
		*expire = strtoll(cynagora->reply.fields[1], NULL, 10);
	else
		*expire = 0;

	return rc;
}

static
int
wait_pending_reply(
	cynagora_t *cynagora
) {
	int rc;
	for (;;) {
		rc = wait_reply(cynagora, true);
		if (rc < 0)
			return rc;
		if (rc > 0 && cynagora->pending == 0)
			return rc;
	}
}

static
int
wait_done(
	cynagora_t *cynagora
) {
	int rc = wait_pending_reply(cynagora);
	if (rc > 0)
		rc = status_done(cynagora);
	return rc;
}

static
int
async(
	cynagora_t *cynagora,
	int op,
	uint32_t events
) {
	return cynagora->async.controlcb && cynagora->fd >= 0
		? cynagora->async.controlcb(cynagora->async.closure, op, cynagora->fd, events)
		: 0;
}

static
void
disconnection(
	cynagora_t *cynagora
) {
	if (cynagora->fd >= 0) {
		async(cynagora, EPOLL_CTL_DEL, 0);
		close(cynagora->fd);
		cynagora->fd = -1;
	}
}

static
int
connection(
	cynagora_t *cynagora
) {
	int rc;

	/* init the client */
	cynagora->pending = 0;
	cynagora->reply.count = -1;
	prot_reset(cynagora->prot);
	cynagora->fd = socket_open(cynagora->socketspec, 0);
	if (cynagora->fd < 0)
		return -errno;

	/* negociate the protocol */
	rc = putxkv(cynagora, _cynagora_, "1", 0, 0);
	if (rc >= 0) {
		rc = wait_pending_reply(cynagora);
		if (rc >= 0) {
			rc = -EPROTO;
			if (cynagora->reply.count >= 2
			 && 0 == strcmp(cynagora->reply.fields[0], _yes_)
			 && 0 == strcmp(cynagora->reply.fields[1], "1")) {
				cache_clear(cynagora->cache,
					cynagora->reply.count > 2 ? (uint32_t)atol(cynagora->reply.fields[2]) : 0);
				rc = async(cynagora, EPOLL_CTL_ADD, EPOLLIN);
				if (rc >= 0)
					return 0;
			}
		}
	}
	disconnection(cynagora);
	return rc;
}

static
int
ensure_opened(
	cynagora_t *cynagora
) {
	if (cynagora->fd >= 0 && write(cynagora->fd, NULL, 0) < 0)
		disconnection(cynagora);
	return cynagora->fd < 0 ? connection(cynagora) : 0;
}

/************************************************************************************/

int
cynagora_open(
	cynagora_t **prcyn,
	cynagora_type_t type,
	uint32_t cache_size,
	const char *socketspec
) {
	cynagora_t *cynagora;
	int rc;

	/* socket spec */
	switch(type) {
	default:
	case cynagora_Check: socketspec = cyn_get_socket_check(socketspec); break;
	case cynagora_Admin: socketspec = cyn_get_socket_admin(socketspec); break;
	case cynagora_Agent: socketspec = cyn_get_socket_agent(socketspec); break;
	}

	/* allocate the structure */
	*prcyn = cynagora = malloc(sizeof *cynagora + 1 + strlen(socketspec));
	if (cynagora == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* create a protocol object */
	rc = prot_create(&cynagora->prot);
	if (rc < 0)
		goto error2;

	/* socket spec */
	strcpy((char*)(cynagora+1), socketspec);

	/* record type and weakly create cache */
	cache_create(&cynagora->cache, CACHESIZE(cache_size));
	cynagora->type = type;
	cynagora->socketspec = socketspec;
	cynagora->async.controlcb = NULL;
	cynagora->async.closure = 0;
	cynagora->async.requests = NULL;

	/* lazy connection */
	cynagora->fd = -1;

	/* done */
	return 0;

error2:
	free(cynagora);
error:
	*prcyn = NULL;
	return rc;
}

void
cynagora_disconnect(
	cynagora_t *cynagora
) {
	disconnection(cynagora);
}

void
cynagora_close(
	cynagora_t *cynagora
) {
	cynagora_async_setup(cynagora, NULL, NULL);
	disconnection(cynagora);
	prot_destroy(cynagora->prot);
	free(cynagora->cache);
	free(cynagora);
}

int
cynagora_enter(
	cynagora_t *cynagora
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (cynagora->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	rc = putxkv(cynagora, _enter_, 0, 0, 0);
	if (rc >= 0)
		rc = wait_done(cynagora);
	return rc;
}

int
cynagora_leave(
	cynagora_t *cynagora,
	bool commit
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (cynagora->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	rc = putxkv(cynagora, _leave_, commit ? _commit_ : 0/*default: rollback*/, 0, 0);
	if (rc >= 0)
		rc = wait_done(cynagora);
	return rc;
}

static
int
check_or_test(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	const char *action
) {
	int rc;
	time_t expire;

	if (cynagora->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	/* ensure there is no clear cache pending */
	flushr(cynagora);

	/* check cache item */
	rc = cache_search(cynagora->cache, key);
	if (rc >= 0)
		return rc;

	/* send the request */
	rc = putxkv(cynagora, action, 0, key, 0);
	if (rc >= 0) {
		/* get the response */
		rc = wait_pending_reply(cynagora);
		if (rc >= 0) {
			rc = status_check(cynagora, &expire);
			if (cynagora->cache && rc >= 0)
				cache_put(cynagora->cache, key, rc, expire);
		}
	}
	return rc;
}

int
cynagora_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key
) {
	return check_or_test(cynagora, key, _check_);
}

int
cynagora_test(
	cynagora_t *cynagora,
	const cynagora_key_t *key
) {
	return check_or_test(cynagora, key, _test_);
}

int
cynagora_set(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	const cynagora_value_t *value
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (cynagora->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	rc = putxkv(cynagora, _set_, 0, key, value);
	if (rc >= 0)
		rc = wait_done(cynagora);
	return rc;
}

int
cynagora_get(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	void (*callback)(
		void *closure,
		const cynagora_key_t *key,
		const cynagora_value_t *value
	),
	void *closure
) {
	int rc;
	cynagora_key_t k;
	cynagora_value_t v;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (cynagora->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	rc = putxkv(cynagora, _get_, 0, key, 0);
	if (rc >= 0) {
		rc = wait_reply(cynagora, true);
		while ((rc == 6 || rc == 7) && !strcmp(cynagora->reply.fields[0], _item_)) {
			k.client = cynagora->reply.fields[1];
			k.session = cynagora->reply.fields[2];
			k.user = cynagora->reply.fields[3];
			k.permission = cynagora->reply.fields[4];
			v.value = cynagora->reply.fields[5];
			v.expire = rc == 6 ? 0 : (time_t)strtoll(cynagora->reply.fields[6], NULL, 10);
			callback(closure, &k, &v);
			rc = wait_reply(cynagora, true);
		}
		rc = status_done(cynagora);
	}
	return rc;
}

int
cynagora_log(
	cynagora_t *cynagora,
	int on,
	int off
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (cynagora->async.requests != NULL)
		return -EINPROGRESS;

	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	rc = putxkv(cynagora, _log_, off ? _off_ : on ? _on_ : 0, 0, 0);
	if (rc >= 0)
		rc = wait_done(cynagora);

	return rc < 0 ? rc : cynagora->reply.count < 2 ? 0 : !strcmp(cynagora->reply.fields[1], _on_);
}


int
cynagora_drop(
	cynagora_t *cynagora,
	const cynagora_key_t *key
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (cynagora->async.requests != NULL)
		return -EINPROGRESS;
	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	rc = putxkv(cynagora, _drop_, 0, key, 0);
	if (rc >= 0)
		rc = wait_done(cynagora);
	return rc;
}

/************************************************************************************/

int
cynagora_cache_resize(
	cynagora_t *cynagora,
	uint32_t size
) {
	return cache_resize(&cynagora->cache, CACHESIZE(size));
}

void
cynagora_cache_clear(
	cynagora_t *cynagora
) {
	cache_clear(cynagora->cache, 0);
}

int
cynagora_cache_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key
) {
	return cache_search(cynagora->cache, key);
}


/************************************************************************************/

int
cynagora_async_setup(
	cynagora_t *cynagora,
	cynagora_async_ctl_t controlcb,
	void *closure
) {
	asreq_t *ar;

	/* cancel pending requests */
	while((ar = cynagora->async.requests) != NULL) {
		cynagora->async.requests = ar->next;
		ar->callback(ar->closure, -ECANCELED);
		free(ar);
	}
	/* remove existing polling */
	async(cynagora, EPOLL_CTL_DEL, 0);
	/* records new data */
	cynagora->async.controlcb = controlcb;
	cynagora->async.closure = closure;
	/* record to polling */
	return async(cynagora, EPOLL_CTL_ADD, EPOLLIN);
}

int
cynagora_async_process(
	cynagora_t *cynagora
) {
	int rc;
	const char *first;
	asreq_t *ar;
	time_t expire;
	cynagora_key_t key;

	for (;;) {
		/* non blocking wait for a reply */
		rc = wait_reply(cynagora, false);
		if (rc < 0)
			return rc == -EAGAIN ? 0 : rc;

		/* skip empty replies */
		if (rc == 0)
			continue;

		/* skip done/error replies */
		first = cynagora->reply.fields[0];
		if (!strcmp(first, _done_)
		 || !strcmp(first, _error_))
			continue;

		/* ignore unexpected answers */
		ar = cynagora->async.requests;
		if (ar == NULL)
			continue;

		/* emit the asynchronous answer */
		cynagora->async.requests = ar->next;
		rc = status_check(cynagora, &expire);
		if (rc >= 0) {
			key.client = (const char*)(ar + 1);
			key.session = &key.client[1 + strlen(key.client)];
			key.user = &key.session[1 + strlen(key.session)];
			key.permission = &key.user[1 + strlen(key.user)];
			cache_put(cynagora->cache, &key, rc, expire);
		}
		ar->callback(ar->closure, rc);
		free(ar);
	}
}

int
cynagora_async_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	int simple,
	void (*callback)(
		void *closure,
		int status),
	void *closure
) {
	int rc;
	asreq_t **pr, *ar;

	rc = ensure_opened(cynagora);
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
	rc = putxkv(cynagora, simple ? _test_ : _check_, 0, key, 0);
	if (rc >= 0)
		rc = flushw(cynagora);
	if (rc < 0) {
		free(ar);
		return rc;
	}

	/* record the request */
	pr = &cynagora->async.requests;
	while(*pr != NULL)
		pr = &(*pr)->next;
	*pr = ar;
	return 0;
}

