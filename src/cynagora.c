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
#include "expire.h"
#include "idgen.h"
#include "names.h"

#define MIN_CACHE_SIZE 400
#define CACHESIZE(x)  ((x) >= MIN_CACHE_SIZE ? (x) : (x) ? MIN_CACHE_SIZE : 0)

static const char syncid[] = "{sync}";

typedef struct asreq asreq_t;
typedef struct ascb  ascb_t;
typedef struct agent agent_t;
typedef struct query query_t;

/** recording of asynchronous request callbacks */
struct ascb
{
	/** link to the next pending callback */
	ascb_t *next;

	/** callback function */
	cynagora_async_check_cb_t *callback;

	/** closure of the callback */
	void *closure;
};

/** recording of asynchronous requests */
struct asreq
{
	/** link to the next pending request */
	asreq_t *next;

	/** callbacks */
	ascb_t *callbacks;

	/** key of the request */
	cynagora_key_t key;

	/** id of the request */
	idgen_t id;
};

/** structure to handle agents */
struct agent
{
	/* Link to the next agent */
	agent_t *next;

	/* recorded callback of the agent */
	cynagora_agent_cb_t *agentcb;

	/* closure of the callback */
	void *closure;

	/* name of the agent */
	char name[];
};

/**
 * structure recording a client
 */
struct cynagora
{
	/** file descriptor of the socket */
	int fd;

	/** synchronous lock */
	bool synclock;

	/** entered in critical section */
	bool entered;

	/** type of link */
	cynagora_type_t type;

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
		cynagora_async_ctl_cb_t *controlcb;

		/** closure */
		void *closure;

		/** requests */
		asreq_t *requests;
	} async;

	/** the declared agents */
	agent_t *agents;

	/** the pending agent queries */
	query_t *queries;

	/** id generator */
	idgen_t idgen;

	/** spec of the socket */
	char socketspec[];
};

/** structure of queries for agents */
struct query
{
	/** public query */
	cynagora_query_t query;

	/** link to the next */
	query_t *next;

	/** the client of the query */
	cynagora_t *cynagora;

	/** the askid */
	char *askid;
};

#if 0
static
int
dump_item(
	void *closure,
	const cynagora_key_t *key,
	int value,
	time_t expire,
	int hit
) {
	fprintf(stderr, ": HIT %3d EXP %6ld VAL %3d <%s %s %s %s>\n",
		hit, expire, value, key->client, key->session, key->user, key->permission);
	return 0;
}

static
int
debug_cache_search(
	cache_t *cache,
	const cynagora_key_t *key
) {
	int rc = cache_search(cache, key);

	fprintf(stderr, "---------------------------------------------------\n");
	fprintf(stderr, "> HAS %3d <%s %s %s %s>\n",
		rc, key->client, key->session, key->user, key->permission);
	cache_iterate(cache, dump_item, NULL);
	fprintf(stderr, "---------------------------------------------------\n");

	return rc;
}

#define cache_search debug_cache_search
#endif

static void agent_ask(cynagora_t *cynagora, int count, const char **fields);
static int async_reply_process(cynagora_t *cynagora, int count);

/**
 * Flush the write buffer of the client
 *
 * @param cynagora  the handler of the client
 *
 * @return  0 in case of success or a negative -errno value
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
 * Send a reply
 *
 * @param cynagora the client
 * @param fields the fields to send
 * @param count the count of fields
 * @return 0 on success or a negative error code
 */
static
int
send_reply(
	cynagora_t *cynagora,
	const char **fields,
	int count
) {
	int rc, trial, i;
	prot_t *prot;

	/* retrieves the protocol handler */
	prot = cynagora->prot;
	trial = 0;
	for(;;) {

		/* fill the fields */
		for (i = rc = 0 ; i < count && rc == 0 ; i++)
			rc = prot_put_field(prot, fields[i]);

		/* send if done */
		if (rc == 0) {
			rc = prot_put_end(prot);
			if (rc == 0) {
				rc = flushw(cynagora);
				break;
			}
		}

		/* failed to fill protocol, cancel current composition  */
		prot_put_cancel(prot);

		/* fail if was last trial */
		if (trial)
			break;

		/* try to flush the output buffer */
		rc = flushw(cynagora);
		if (rc)
			break;

		trial = 1;
	}
	return rc;
}

/**
 * Put the command made of arguments ...
 * Increment the count of pending requests.
 *
 * @param cynagora  the handler of the client
 * @param command   the command to send
 * @param optarg    an optional argument or NULL
 * @param optkey    an optional key or NULL
 * @param optval    an optional value or NULL
 *
 * @return  0 in case of success or a negative -errno value
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
	int nf, rc;
	char text[30];
	const char *fields[8];

	/* prepare fields */
	fields[0] = command;
	nf = 1;
	if (optarg)
		fields[nf++] = optarg;
	if (optkey) {
		fields[nf++] = optkey->client;
		fields[nf++] = optkey->session;
		fields[nf++] = optkey->user;
		fields[nf++] = optkey->permission;
	}
	if (optval) {
		fields[nf++] = optval->value;
		if (optval->expire) {
			exp2txt(optval->expire, true, text, sizeof text);
			fields[nf++] = text;
		}
	}

	/* send now */
	rc = send_reply(cynagora, fields, nf);
	return rc;
}

/**
 * Calls the asynchronous control callback with operation and the given events
 *
 * @param cynagora  the handler of the client
 * @param op operation (see epoll_ctl)
 * @param events the events (see epoll_ctl)
 *
 * @return  0 in case of success or a negative -errno value
 */
static
int
async_control(
	cynagora_t *cynagora,
	int op,
	uint32_t events
) {
	return cynagora->async.controlcb && cynagora->fd >= 0
		? cynagora->async.controlcb(cynagora->async.closure, op, cynagora->fd, events)
		: 0;
}

/**
 * Wait some input event
 *
 * @param cynagora  the handler of the client
 *
 * @return  0 in case of success or a negative -errno value
 */
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

/**
 * Get the next reply if any
 *
 * @param cynagora  the handler of the client
 *
 * @return  the count of field of the reply (can be 0)
 *          or -EAGAIN if there is no reply
 */
static
int
get_reply(
	cynagora_t *cynagora
) {
	int rc;
	const char *first;
	uint32_t cacheid;

	prot_next(cynagora->prot);
	rc = prot_get(cynagora->prot, &cynagora->reply.fields);
	if (rc > 0) {
		first = cynagora->reply.fields[0];
		if (0 == strcmp(first, _clear_)) {
			/* clearing the cache */
			cacheid = rc > 1 ? (uint32_t)atol(cynagora->reply.fields[1]) : 0;
			cache_clear(cynagora->cache, cacheid);
			rc = 0;
		} else if (0 == strcmp(first, _ask_)) {
			/* on asking agent */
			agent_ask(cynagora, rc - 1, &cynagora->reply.fields[1]);
			rc = 0;
		} else {
			if (0 != strcmp(cynagora->reply.fields[0], _item_)) {
				if (strcmp(first, _done_) && strcmp(first, _error_)) {
					if (async_reply_process(cynagora, rc))
						rc = 0;
				}
			}
		}
	}
	cynagora->reply.count = rc;
	return rc;
}

/**
 * Wait for a reply
 *
 * @param cynagora  the handler of the client
 * @param block
 *
 * @return  the count of fields greater than 0 or a negative -errno value
 *          or -EAGAIN if nothing and block == false
 *          or -EPIPE if broken link
 */
static
int
wait_reply(
	cynagora_t *cynagora,
	bool block
) {
	int rc;

	for(;;) {
		/* get the next reply if any */
		rc = get_reply(cynagora);
		if (rc > 0)
			return rc;

		if (rc < 0) {
			/* wait for an answer */
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

/**
 * Enter synchronous section
 *
 * @param cynagora  the handler of the client
 *
 * @return 1 if entered or 0 if not entered
 */
static
bool
synchronous_enter(
	cynagora_t *cynagora
) {
	if (cynagora->synclock)
		return false;
	cynagora->synclock = true;
	async_control(cynagora, EPOLL_CTL_MOD, 0);
	return true;
}

/**
 * Enter synchronous section
 *
 * @param cynagora  the handler of the client
 *
 * @return 1 if entered or 0 if not entered
 */
static
int
synchronous_leave(
	cynagora_t *cynagora,
	int rc
) {
	async_control(cynagora, EPOLL_CTL_MOD, EPOLLIN);
	cynagora->synclock = false;
	return rc;
}

/**
 * Read and process any input data
 *
 * @param cynagora the client handler
 *
 * @return 0 on success or a negative -errno error code
 */
static
int
flushr(
	cynagora_t *cynagora
) {
	int rc;

	do { rc = wait_reply(cynagora, false); } while(rc > 0);
	return rc;
}

/**
 * Test if the first field is "done"
 *
 * @param cynagora  the handler of the client
 *
 * @return  0 if done or -ECANCELED otherwise
 */
static
int
status_done(
	cynagora_t *cynagora
) {
	return strcmp(cynagora->reply.fields[0], _done_) ? -ECANCELED : 0;
}

/**
 * Translates the check/test reply to a forbiden/granted status
 *
 * @param cynagora  the handler of the client
 * @param expire    where to store the expiration read
 *
 * @return  0 in case of success or a negative -errno value
 */
static
int
status_check(
	cynagora_t *cynagora,
	int count,
	time_t *expire
) {
	int rc;

	if (!strcmp(cynagora->reply.fields[0], _yes_))
		rc = 1;
	else if (!strcmp(cynagora->reply.fields[0], _no_))
		rc = 0;
	else if (!strcmp(cynagora->reply.fields[0], _ack_))
		rc = -EEXIST;
	else
		rc = -EPROTO;

	if (count < 3)
		*expire = 0;
	else if (cynagora->reply.fields[2][0] == '-')
		*expire = -1;
	else
		txt2exp(cynagora->reply.fields[2], expire, true);

	return rc;
}

/**
 * Wait for a reply
 *
 * @param cynagora  the handler of the client
 *
 * @return  0 in case of success or a negative -errno value
 */
static
int
wait_any_reply(
	cynagora_t *cynagora
) {
	int rc;
	for (;;) {
		rc = wait_reply(cynagora, true);
		if (rc < 0)
			return rc;
		if (rc > 0)
			return rc;
	}
}

/**
 * Wait the reply "done"
 *
 * @param cynagora  the handler of the client
 *
 * @return  0 in case of success or a negative -errno value
 *          -ECANCELED when received an error status
 */
static
int
wait_done(
	cynagora_t *cynagora
) {
	int rc = wait_any_reply(cynagora);
	if (rc > 0)
		rc = status_done(cynagora);
	return rc;
}

/**
 * Disconnect the client
 *
 * @param cynagora  the handler of the client
 */
static
void
disconnection(
	cynagora_t *cynagora
) {
	query_t *query;

	if (cynagora->fd >= 0) {
		/* forget queries */
		query = cynagora->queries;
		cynagora->queries = 0;
		while(query) {
			query->cynagora = 0;
			query = query->next;
		}
		/* drop connection */
		async_control(cynagora, EPOLL_CTL_DEL, 0);
		close(cynagora->fd);
		cynagora->fd = -1;
	}
}

/**
 * connect the client
 *
 * @param cynagora  the handler of the client
 *
 * @return  0 in case of success or a negative -errno value
 */
static
int
connection(
	cynagora_t *cynagora
) {
	int rc;
	agent_t *agent;

	/* init the client */
	cynagora->reply.count = -1;
	prot_reset(cynagora->prot);
	cynagora->fd = socket_open(cynagora->socketspec, 0);
	if (cynagora->fd < 0)
		return -errno;

	/* negociate the protocol */
	rc = putxkv(cynagora, _cynagora_, "1", 0, 0);
	if (rc >= 0) {
		rc = wait_any_reply(cynagora);
		if (rc >= 0) {
			rc = -EPROTO;
			if (cynagora->reply.count >= 2
			 && 0 == strcmp(cynagora->reply.fields[0], _done_)
			 && 0 == strcmp(cynagora->reply.fields[1], "1")) {
				cache_clear(cynagora->cache,
					cynagora->reply.count > 2 ? (uint32_t)atol(cynagora->reply.fields[2]) : 0);
				rc = async_control(cynagora, EPOLL_CTL_ADD, EPOLLIN);
				/* reconnect agent */
				agent = cynagora->agents;
				while (agent && rc >= 0) {
					rc = putxkv(cynagora, _agent_, agent->name, 0, 0);
					if (rc >= 0)
						rc = wait_done(cynagora);
					agent = agent->next;
				}
				if (rc >= 0) {
					return 0;
				}
			}
		}
	}
	disconnection(cynagora);
	return rc;
}

/**
 * ensure the connection is opened
 *
 * @param cynagora  the handler of the client
 *
 * @return  0 in case of success or a negative -errno value
 */
static
int
ensure_opened(
	cynagora_t *cynagora
) {
	if (cynagora->fd >= 0 && write(cynagora->fd, NULL, 0) < 0)
		disconnection(cynagora);
	return cynagora->fd < 0 ? connection(cynagora) : 0;
}

/**
 * Check or test synchronously
 *
 * @param cynagora  the handler of the client
 * @param key       the key to test/check
 * @param force     if not set forbids cache use
 * @param action    test or check
 *
 * @return  0 in case of success or a negative -errno value
 */
static
int
check_or_test(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	int force,
	const char *action
) {
	int rc;
	time_t expire;

	if (!synchronous_enter(cynagora))
		return -EBUSY;

	/* ensure opened */
	rc = ensure_opened(cynagora);
	if (rc < 0)
		goto end;

	/* check cache item */
	if (!force) {
		/* ensure there is no clear cache pending */
		flushr(cynagora);

		rc = cache_search(cynagora->cache, key);
		if (rc >= 0)
			goto end;
	}

	/* send the request */
	rc = putxkv(cynagora, action, syncid, key, 0);
	if (rc >= 0) {
		/* get the response */
		rc = wait_any_reply(cynagora);
		if (rc >= 0) {
			rc = status_check(cynagora, rc, &expire);
			if (rc >= 0 && action == _check_)
				cache_put(cynagora->cache, key, rc, expire, true);
		}
	}
end:
	return synchronous_leave(cynagora, rc);

}

/**
 * get the pending asynchrounous request
 *
 * @param cynagora the cynagora client
 * @param id       id of the request to find
 * @param unlink   if true, remove the request from the
 *                 list of requests
 * @return the found request of NULL
 */
static
asreq_t *
search_async_request(
	cynagora_t *cynagora,
	const char *id,
	bool unlink
) {
	asreq_t *ar, **par;

	par = &cynagora->async.requests;
	while((ar = *par) && strcmp(ar->id, id))
		par = &ar->next;
	if (ar && unlink)
		*par = ar->next;
	return ar;
}

static
int
async_reply_process(
	cynagora_t *cynagora,
	int count
) {
	int status;
	const char *id;
	asreq_t *ar;
	ascb_t *ac;
	time_t expire;

	id = count < 2 ? "" : cynagora->reply.fields[1];
	ar = search_async_request(cynagora, id, true);

	if (!ar)
		return 0;

	/* emit the asynchronous answer */
	status = status_check(cynagora, count, &expire);
	if (status >= 0)
		cache_put(cynagora->cache, &ar->key, status, expire, true);
	while((ac = ar->callbacks) != NULL) {
		ar->callbacks = ac->next;
		ac->callback(ac->closure, status);
		free(ac);
	}
	free(ar);
	return 1;
}

static
int
async_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	int force,
	int simple,
	cynagora_async_check_cb_t *callback,
	void *closure,
	const char *askid
) {
	int rc;
	asreq_t *ar;
	ascb_t *ac;
	char *p;
	int nf;
	const char *fields[8];

	/* ensure connection */
	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	/* check cache item */
	if (!force) {
		/* ensure there is no clear cache pending */
		flushr(cynagora);

		rc = cache_search(cynagora->cache, key);
		if (rc >= 0) {
			callback(closure, rc);
			return 0;
		}
	}

	/* allocates the callback */
	ac = malloc(sizeof *ac);
	if (ac == NULL)
		return -ENOMEM;
	ac->callback = callback;
	ac->closure = closure;

	/* common request only if not subqueries of agents */
	if (!askid) {
		/* search the request */
		ar = cynagora->async.requests;
		while (ar && (strcmp(key->client, ar->key.client)
			|| strcmp(key->session, ar->key.session)
			|| strcmp(key->user, ar->key.user)
			|| strcmp(key->permission, ar->key.permission)))
			ar = ar->next;

		/* a same request is pending, use it */
		if (ar) {
			ac->next = ar->callbacks;
			ar->callbacks = ac;
			return 0;
		}
	}

	/* allocate for the request */
	ar = malloc(sizeof *ar + strlen(key->client) + strlen(key->session) + strlen(key->user) + strlen(key->permission) + 4);
	if (ar == NULL) {
		free(ac);
		return -ENOMEM;
	}

	/* init */
	ac->next = NULL;
	ar->callbacks = ac;
	p = (char*)(ar + 1);
	ar->key.client = p;
	p = stpcpy(p, key->client) + 1;
	ar->key.session = p;
	p = stpcpy(p, key->session) + 1;
	ar->key.user = p;
	p = stpcpy(p, key->user) + 1;
	ar->key.permission = p;
	stpcpy(p, key->permission);
	do {
		idgen_next(cynagora->idgen);
	} while (search_async_request(cynagora, cynagora->idgen, false));
	strcpy(ar->id, cynagora->idgen);
	ar->next = cynagora->async.requests;
	cynagora->async.requests = ar;

	/* send the request */
	if (askid) {
		fields[0] = _sub_;
		fields[1] = askid;
		nf = 2;
	} else {
		fields[0] = simple ? _test_ : _check_;
		nf = 1;
	}
	fields[nf++] = ar->id;
	fields[nf++] = key->client;
	fields[nf++] = key->session;
	fields[nf++] = key->user;
	fields[nf++] = key->permission;
	rc = send_reply(cynagora, fields, nf);
	if (rc < 0) {
		ar = search_async_request(cynagora, ar->id, true);
		while((ac = ar->callbacks)) {
			ar->callbacks = ac->next;
			free(ac);
		}
		free(ar);
		return rc;
	}

	/* record the request */
	return 0;
}

/******************************************************************************/
/*** PUBLIC COMMON METHODS                                                  ***/
/******************************************************************************/

/* see cynagora.h */
int
cynagora_create(
	cynagora_t **prcyn,
	cynagora_type_t type,
	uint32_t cache_size,
	const char *socketspec
) {
	cynagora_t *cynagora;
	int rc;

	/* socket spec */
	switch(type) {
	case cynagora_Admin:
		socketspec = cyn_get_socket_admin(socketspec);
		break;

	case cynagora_Agent:
		socketspec = cyn_get_socket_agent(socketspec);
		break;

	case cynagora_Check:
	default:
		socketspec = cyn_get_socket_check(socketspec);
		break;
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
	strcpy(cynagora->socketspec, socketspec);

	/* record type and weakly create cache */
	cache_create(&cynagora->cache, CACHESIZE(cache_size)); /* ignore errors */
	cynagora->entered = false;
	cynagora->synclock = false;
	cynagora->type = type;
	cynagora->async.controlcb = NULL;
	cynagora->async.closure = 0;
	cynagora->async.requests = NULL;
	cynagora->agents = NULL;
	cynagora->queries = NULL;
	idgen_init(cynagora->idgen);

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

/* see cynagora.h */
void
cynagora_disconnect(
	cynagora_t *cynagora
) {
	disconnection(cynagora);
}

/* see cynagora.h */
void
cynagora_destroy(
	cynagora_t *cynagora
) {
	cynagora_async_setup(cynagora, NULL, NULL);
	disconnection(cynagora);
	prot_destroy(cynagora->prot);
	free(cynagora->cache);
	free(cynagora);
}

/* see cynagora.h */
int
cynagora_async_setup(
	cynagora_t *cynagora,
	cynagora_async_ctl_cb_t *controlcb,
	void *closure
) {
	asreq_t *ar;
	ascb_t *ac;

	/* cancel pending requests */
	while((ar = cynagora->async.requests) != NULL) {
		cynagora->async.requests = ar->next;
		while((ac = ar->callbacks) != NULL) {
			ar->callbacks = ac->next;
			ac->callback(ac->closure, -ECANCELED);
			free(ac);
		}
		free(ar);
	}

	/* remove existing polling */
	async_control(cynagora, EPOLL_CTL_DEL, 0);

	/* records new data */
	cynagora->async.closure = closure;
	cynagora->async.controlcb = controlcb;

	/* record to polling */
	return async_control(cynagora, EPOLL_CTL_ADD, EPOLLIN);
}

/* see cynagora.h */
int
cynagora_async_process(
	cynagora_t *cynagora
) {
	int rc;

	for (;;) {
		/* non blocking wait for a reply */
		rc = wait_reply(cynagora, false);
		if (rc < 0)
			return rc == -EAGAIN ? 0 : rc;
	}
}

/* see cynagora.h */
int
cynagora_cache_resize(
	cynagora_t *cynagora,
	uint32_t size
) {
	return cache_resize(&cynagora->cache, CACHESIZE(size));
}

/* see cynagora.h */
void
cynagora_cache_clear(
	cynagora_t *cynagora
) {
	cache_clear(cynagora->cache, 0);
}

/* see cynagora.h */
int
cynagora_cache_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key
) {
	/* ensure there is no clear cache pending */
	flushr(cynagora);
	return cache_search(cynagora->cache, key);
}

/* see cynagora.h */
int
cynagora_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	int force
) {
	return check_or_test(cynagora, key, force, _check_);
}

/* see cynagora.h */
int
cynagora_test(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	int force
) {
	return check_or_test(cynagora, key, force, _test_);
}

/* see cynagora.h */
int
cynagora_async_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	int force,
	int simple,
	cynagora_async_check_cb_t *callback,
	void *closure
) {
	return async_check(cynagora, key, force, simple, callback, closure, NULL);
}

/******************************************************************************/
/*** PUBLIC ADMIN METHODS                                                   ***/
/******************************************************************************/

/* see cynagora.h */
int
cynagora_get(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	cynagora_get_cb_t *callback,
	void *closure
) {
	int rc;
	cynagora_key_t k;
	cynagora_value_t v;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;

	if (!synchronous_enter(cynagora))
		return -EBUSY;

	rc = ensure_opened(cynagora);
	if (rc >= 0) {
		rc = putxkv(cynagora, _get_, 0, key, 0);
		if (rc >= 0) {
			rc = wait_reply(cynagora, true);
			while ((rc == 6 || rc == 7) && !strcmp(cynagora->reply.fields[0], _item_)) {
				k.client = cynagora->reply.fields[1];
				k.session = cynagora->reply.fields[2];
				k.user = cynagora->reply.fields[3];
				k.permission = cynagora->reply.fields[4];
				v.value = cynagora->reply.fields[5];
				if (rc == 6)
					v.expire = 0;
				else if (!txt2exp(cynagora->reply.fields[6], &v.expire, true))
					v.expire = -1;
				callback(closure, &k, &v);
				rc = wait_reply(cynagora, true);
			}
			rc = status_done(cynagora);
		}
	}
	return synchronous_leave(cynagora, rc);
}

/* see cynagora.h */
int
cynagora_log(
	cynagora_t *cynagora,
	int on,
	int off
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;

	if (!synchronous_enter(cynagora))
		return -EBUSY;

	rc = ensure_opened(cynagora);
	if (rc >= 0) {
		rc = putxkv(cynagora, _log_, off ? _off_ : on ? _on_ : 0, 0, 0);
		if (rc >= 0) {
			rc = wait_done(cynagora);
			if (rc > 0)
				rc = cynagora->reply.count >= 2 && !strcmp(cynagora->reply.fields[1], _on_);
		}
	}
	if (rc >= 0)
		rc = cynagora->reply.count < 2 ? 0 : !strcmp(cynagora->reply.fields[1], _on_);

	return synchronous_leave(cynagora, rc);
}

/* see cynagora.h */
int
cynagora_enter(
	cynagora_t *cynagora
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (cynagora->entered)
		return -ECANCELED;

	if (!synchronous_enter(cynagora))
		return -EBUSY;

	rc = ensure_opened(cynagora);
	if (rc >= 0) {
		rc = putxkv(cynagora, _enter_, 0, 0, 0);
		if (rc >= 0) {
			rc = wait_done(cynagora);
			if (rc >= 0)
				cynagora->entered = true;
		}
	}
	return synchronous_leave(cynagora, rc);
}

/* see cynagora.h */
int
cynagora_leave(
	cynagora_t *cynagora,
	int commit
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (!cynagora->entered)
		return -ECANCELED;

	if (!synchronous_enter(cynagora))
		return -EBUSY;

	rc = ensure_opened(cynagora);
	if (rc >= 0) {
		rc = putxkv(cynagora, _leave_, commit ? _commit_ : 0/*default: rollback*/, 0, 0);
		if (rc >= 0) {
			rc = wait_done(cynagora);
			if (rc >= 0)
				cynagora->entered = false;
		}
	}
	return synchronous_leave(cynagora, rc);
}

/* see cynagora.h */
int
cynagora_set(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	const cynagora_value_t *value
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (!cynagora->entered)
		return -ECANCELED;

	if (!synchronous_enter(cynagora))
		return -EBUSY;

	rc = ensure_opened(cynagora);
	if (rc >= 0) {
		rc = putxkv(cynagora, _set_, 0, key, value);
		if (rc >= 0)
			rc = wait_done(cynagora);
	}
	return synchronous_leave(cynagora, rc);
}

/* see cynagora.h */
int
cynagora_drop(
	cynagora_t *cynagora,
	const cynagora_key_t *key
) {
	int rc;

	if (cynagora->type != cynagora_Admin)
		return -EPERM;
	if (!cynagora->entered)
		return -ECANCELED;

	if (!synchronous_enter(cynagora))
		return -EBUSY;

	rc = ensure_opened(cynagora);
	if (rc >= 0) {
		rc = putxkv(cynagora, _drop_, 0, key, 0);
		if (rc >= 0)
			rc = wait_done(cynagora);
	}
	return synchronous_leave(cynagora, rc);
}

/******************************************************************************/
/*** PUBLIC ADMIN AND AGENT METHODS                                         ***/
/******************************************************************************/

/* see cynagora.h */
int
cynagora_clearall(
	cynagora_t *cynagora
) {
	int rc;

	if (cynagora->type != cynagora_Admin && cynagora->type != cynagora_Agent)
		return -EPERM;

	if (!synchronous_enter(cynagora))
		return -EBUSY;

	rc = ensure_opened(cynagora);
	if (rc >= 0) {
		rc = putxkv(cynagora, _clearall_, 0, 0, 0);
		if (rc >= 0) {
			rc = wait_done(cynagora);
		}
	}
	return synchronous_leave(cynagora, rc);
}

/******************************************************************************/
/*** PRIVATE AGENT METHODS                                                  ***/
/******************************************************************************/

/**
 * Search the recorded agent of name
 *
 * @param cynagora the client cynagora
 * @param name the name of the agent
 * @param unlink should unlink from the link
 * @return the found agent or NULL
 */
static
agent_t*
agent_search(
	cynagora_t *cynagora,
	const char *name,
	bool unlink
) {
	agent_t *r, **pr;

	pr = &cynagora->agents;
	while((r = *pr) && strcmp(name, r->name))
		pr = &r->next;
	if (r && unlink)
		*pr = r->next;
	return r;
}

/**
 * Send an agent reply
 *
 * @param cynagora the client
 * @param askid the ask identifier
 * @param value the value to return
 * @param expire the expiration of the value
 * @return 0 on success or a negative error code
 */
static
int
agent_send_reply(
	cynagora_t *cynagora,
	const char *askid,
	const char *value,
	time_t expire
) {
	int nf;
	char text[30];
	const char *fields[4];

	fields[0] = _reply_;
	fields[1] = askid;
	fields[2] = value;

	/* format expiration */
	if (!expire)
		nf = 3;
	else {
		exp2txt(expire, true, text, sizeof text);
		fields[3] = text;
		nf = 4;
	}
	return send_reply(cynagora, fields, nf);
}

/**
 * Dispatch a received agent request
 *
 * The received fields should be:
 *
 *    ASKID NAME VALUE CLIENT SESSION USER PERMISSION
 *
 * @param cynagora the handler
 * @param count the count of fields
 * @param fields the fields
 */
static
void
agent_ask(
	cynagora_t *cynagora,
	int count,
	const char **fields
) {
	int rc;
	size_t length;
	agent_t *agent;
	query_t *query;
	char *p;

	if (count != 7)
		goto error;

	/* search the agent */
	agent = agent_search(cynagora, fields[1], false);
	if (!agent)
		goto error;

	length = strlen(fields[0]);
	length += strlen(fields[1]);
	length += strlen(fields[2]);
	length += strlen(fields[3]);
	length += strlen(fields[4]);
	length += strlen(fields[5]);
	length += strlen(fields[6]);

	query = malloc(length + 7 + sizeof *query);
	if (!query)
		goto error;
	p = (char *)&query[1];

	query->askid = p;
	p = 1 + stpcpy(p, fields[0]);

	query->query.name = p;
	p = 1 + stpcpy(p, fields[1]);

	query->query.value = p;
	p = 1 + stpcpy(p, fields[2]);

	query->query.key.client = p;
	p = 1 + stpcpy(p, fields[3]);

	query->query.key.session = p;
	p = 1 + stpcpy(p, fields[4]);

	query->query.key.user = p;
	p = 1 + stpcpy(p, fields[5]);

	query->query.key.permission = p;
	p = 1 + stpcpy(p, fields[6]);

	query->cynagora = cynagora;
	query->next = cynagora->queries;
	cynagora->queries = query;

	rc = agent->agentcb(agent->closure, &query->query);
	if (rc < 0)
		cynagora_agent_reply(&query->query, NULL);
	return;
error:
	agent_send_reply(cynagora, count ? fields[0] : "0", _error_, -1);
}

/******************************************************************************/
/*** PUBLIC AGENT METHODS                                                   ***/
/******************************************************************************/

int
cynagora_agent_is_valid_name(
	const char *name
) {
	return agent_check_name(name) != 0;
}

/* see cynagora.h */
int
cynagora_agent_create(
	cynagora_t *cynagora,
	const char *name,
	cynagora_agent_cb_t *agentcb,
	void *closure
) {
	int rc;
	size_t length;
	agent_t *agent;

	/* check validity */
	if (cynagora->type != cynagora_Agent)
		return -EPERM;

	/* check name */
	length = (size_t)agent_check_name(name);
	if (!length)
		return -EINVAL;

	/* ensure connection */
	rc = ensure_opened(cynagora);
	if (rc < 0)
		return rc;

	/* allocate agent */
	agent = malloc(length + 1 + sizeof *agent);
	if (!agent)
		return -ENOMEM;

	/* init the structure */
	agent->agentcb = agentcb;
	agent->closure = closure;
	memcpy(agent->name, name, 1 + length);
	agent->next = cynagora->agents;
	cynagora->agents = agent;

	/* send the command */
	rc = putxkv(cynagora, _agent_, name, 0, 0);
	if (rc >= 0)
		rc = wait_done(cynagora);
	if (rc < 0) {
		/* clean on error */
		agent_search(cynagora, name, true);
		free(agent);
	}

	return rc;
}

/* see cynagora.h */
int
cynagora_agent_reply(
	cynagora_query_t *_query,
	cynagora_value_t *value
) {
	int rc;
	query_t *query = (query_t*)_query;
	query_t **p;
	cynagora_t *cynagora;

	cynagora = query->cynagora;
	if (!cynagora)
		rc = -ECANCELED;
	else {
		/* unlink the query */
		p = &cynagora->queries;
		while (*p)
			if (*p != query)
				p = &(*p)->next;
			else {
				*p = query->next;
				break;
			}

		/* send the reply */
		rc = agent_send_reply(cynagora, query->askid,
			value ? value->value : _error_,
			value ? value->expire : -1);
	}
	free(query);
	return rc;
}

/* see cynagora.h */
int
cynagora_agent_subquery_async(
	cynagora_query_t *_query,
	const cynagora_key_t *key,
	int force,
	cynagora_async_check_cb_t *callback,
	void *closure
) {
	int rc;
	query_t *query = (query_t*)_query;
	cynagora_t *cynagora;

	cynagora = query->cynagora;
	if (!cynagora)
		rc = -ECANCELED;
	else
		rc = async_check(cynagora, key, force, false,
					callback, closure, query->askid);
	return rc;
}
