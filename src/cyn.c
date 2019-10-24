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
/* IMPLEMENTATION OF LOCAL CYNAGORA API                                       */
/******************************************************************************/
/******************************************************************************/

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "data.h"
#include "db.h"
#include "queue.h"
#include "cyn.h"
#include "names.h"

#if !CYN_SEARCH_DEEP_MAX
# define CYN_SEARCH_DEEP_MAX 10
#endif
#if !defined(AGENT_SEPARATOR_CHARACTER)
# define AGENT_SEPARATOR_CHARACTER ':'
#endif

/**
 * items of the list of observers or awaiters
 */
struct callback
{
	/** link to the next item of the list */
	struct callback *next;

	/** recording of callback function */
	union {
		/** any callback value */
		void *any_cb;

		/** awaiter callback */
		on_enter_cb_t *on_enter_cb;

		/** observer callback */
		on_change_cb_t *on_change_cb;
	};

	/** closure of the callback */
	void *closure;
};

/**
 * items of the list of agents
 */
struct agent
{
	/** link to the next item of the list */
	struct agent *next;

	/** agent callback */
	agent_cb_t *agent_cb;

	/** closure of the callback */
	void *closure;

	/** length of the name (without terminating zero) */
	uint8_t len;

	/** name of the agent */
	char name[];
};

/**
 * structure handling an asynchronous requests
 */
struct cynagora_query
{
	/** callback for handling result of the check */
	on_result_cb_t *on_result_cb;

	/** closure of the callback */
	void *closure;

	/** key of the check */
	data_key_t key;

	/** down counter for recursivity limitation */
	int decount;
};

/** for locking critical section with magic */
static const void *magic_locker;

/** head of the list of critical section awaiters */
static struct callback *awaiters;

/** head of the list of change observers */
static struct callback *observers;

/** head of the list of recorded agents */
static struct agent *agents;

/** holding of changeid */
static struct {
	/** current changeid */
	uint32_t current;

	/** changeid set in string */
	uint32_t instring;

	/** string value for a changeid */
	char string[12];
} changeid = {
	.current = 1,
	.instring = 0,
	.string = { 0 }
};

/**
 * Delete from the list represented by 'head' the entry for
 * 'callback' and 'closure'
 * @param callback
 * @param closure
 * @param head
 * @return 0 if entry found and removed, -ENOENT if not found
 */
static
int
delcb(
	void *callback,
	void *closure,
	struct callback **head
) {
	struct callback *c;

	while((c = *head)) {
		if (c->any_cb == callback && c->closure == closure) {
			*head = c->next;
			free(c);
			return 0;
		}
		head = &c->next;
	}
	return -ENOENT;
}

/**
 * Adds at head of the list represented by 'head' the entry for
 * 'callback' and 'closure'
 * @param callback
 * @param closure
 * @param head
 * @return 0 if correctly added or -ENOMEM in case of allocation failure
 */
static
int
addcb(
	void *callback,
	void *closure,
	struct callback **head
) {
	struct callback *c;

	c = malloc(sizeof *c);
	if (c == NULL)
		return -ENOMEM;
	c->any_cb = callback;
	c->closure = closure;
	c->next = *head;
	*head = c;
	return 0;
}

/**
 * Call when database changed.
 * Calls all observers to notify them the change
 */
static
void
changed(
) {
	struct callback *c;

	changeid.current = changeid.current + 1 ?: 1;
	for (c = observers; c ; c = c->next)
		c->on_change_cb(c->closure);
}

/* see cyn.h */
int
cyn_enter(
	const void *magic
) {
	if (!magic)
		return -EINVAL;
	if (magic_locker)
		return -EBUSY;
	magic_locker = magic;
	return 0;
}

/* see cyn.h */
int
cyn_enter_async(
	on_enter_cb_t *enter_cb,
	void *magic
) {
	if (!magic)
		return -EINVAL;
	if (magic_locker)
		return addcb(enter_cb, magic, &awaiters);

	magic_locker = magic;
	enter_cb(magic);
	return 0;
}

/* see cyn.h */
int
cyn_enter_async_cancel(
	on_enter_cb_t *enter_cb,
	void *closure
) {
	return delcb(enter_cb, closure, &awaiters);
}

/* see cyn.h */
int
cyn_on_change_add(
	on_change_cb_t *on_change_cb,
	void *closure
) {
	return addcb(on_change_cb, closure, &observers);
}

/* see cyn.h */
int
cyn_on_change_remove(
	on_change_cb_t *on_change_cb,
	void *closure
) {
	return delcb(on_change_cb, closure, &observers);
}

/* see cyn.h */
int
cyn_leave(
	const void *magic,
	bool commit
) {
	int rc, rcp;
	struct callback *e, **p;

	if (!magic)
		return -EINVAL;
	if (!magic_locker)
		return -EALREADY;
	if (magic_locker != magic)
		return -EPERM;

	magic_locker = &magic_locker;
	if (!commit)
		rc = 0;
	else {
		rc = db_transaction_begin();
		if (rc == 0) {
			rcp = queue_play();
			rc = db_transaction_end(rcp == 0) ?: rcp;
			if (rcp == 0)
				changed();
		}
	}
	queue_clear();

	/* wake up awaiting client */
	e = awaiters;
	if (!e)
		magic_locker = 0;
	else {
		/* the one to awake is at the end of the list */
		p = &awaiters;
		while(e->next) {
			p = &e->next;
			e = *p;
		}
		*p = NULL;
		magic_locker = e->closure;
		e->on_enter_cb(e->closure);
		free(e);
	}

	return rc;
}

/* see cyn.h */
int
cyn_set(
	const data_key_t *key,
	const data_value_t *value
) {
	if (!magic_locker)
		return -EPERM;
	return queue_set(key, value);
}

/* see cyn.h */
int
cyn_drop(
	const data_key_t *key
) {
	if (!magic_locker)
		return -EPERM;
	return queue_drop(key);
}

/* see cyn.h */
void
cyn_list(
	list_cb_t *callback,
	void *closure,
	const data_key_t *key
) {
	db_for_all(callback, closure, key);
}

/**
 * initialize value to its default
 *
 * @param value to initialize
 * @return the initialized value
 */
static
data_value_t *
default_value(
	data_value_t *value
) {
	value->value = DEFAULT;
	value->expire = 0;
	return value;
}

/**
 * Search the agent of name and return its item in the list
 *
 * @param name of the agent to find (optionally zero terminated)
 * @param len length of the name (without terminating zero)
 * @param ppprev for catching the pointer referencing the return item
 * @return 0 if not found or the pointer to the item of the found agent
 */
static
struct agent *
search_agent(
	const char *name,
	size_t length,
	struct agent ***ppprev
) {
	struct agent *it, **pprev;

	pprev = &agents;
	while((it = *pprev)
	  &&  ((uint8_t)length != it->len || memcmp(it->name, name, length)))
		pprev = &it->next;
	*ppprev = pprev;
	return it;
}

/**
 * Return the agent required by the value or NULL if no agent is required
 * or if the agent is not found.
 *
 * @param value string where agent is the prefix followed by one colon
 * @return the item of the required agent or NULL when no agent is required
 */
static
struct agent*
required_agent(
	const char *value
) {
	struct agent **pprev;
	size_t length;

	for (length = 0 ; length <= UINT8_MAX && value[length] ; length++)
		if (value[length] == AGENT_SEPARATOR_CHARACTER)
			return search_agent(value, length, &pprev);
	return NULL;
}

/**
 * Allocates the query structure for handling the given parameters
 * and return it. The query structure copies the key data to be compatible
 * with asynchronous processing.
 *
 * @param on_result_cb the result callback to record
 * @param closure the closure for the result callback
 * @param key the key of the query
 * @param maxdepth maximum depth of the agent subrequests
 * @return the allocated structure or NULL in case of memory depletion
 */
static
cynagora_query_t *
alloc_query(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key,
	int maxdepth
) {
	size_t szcli, szses, szuse, szper;
	cynagora_query_t *query;
	void *ptr;

	/* allocate asynchronous query */
	szcli = key->client ? 1 + strlen(key->client) : 0;
	szses = key->session ? 1 + strlen(key->session) : 0;
	szuse = key->user ? 1 + strlen(key->user) : 0;
	szper = key->permission ? 1 + strlen(key->permission) : 0;
	query = malloc(szcli + szses + szuse + szper + sizeof *query);
	if (query) {
		/* init the structure */
		ptr = &query[1];
		query->on_result_cb = on_result_cb;
		query->closure = closure;
		query->decount = maxdepth;

		/* copy strings of the key */
		if (!key->client)
			query->key.client = 0;
		else {
			query->key.client = ptr;
			ptr = mempcpy(ptr, key->client, szcli);
		}
		if (!key->session)
			query->key.session = 0;
		else {
			query->key.session = ptr;
			ptr = mempcpy(ptr, key->session, szses);
		}
		if (!key->user)
			query->key.user = 0;
		else {
			query->key.user = ptr;
			ptr = mempcpy(ptr, key->user, szuse);
		}
		if (!key->permission)
			query->key.permission = 0;
		else {
			query->key.permission = ptr;
			mempcpy(ptr, key->permission, szper);
		}
	}
	return query;
}

/* see cyn.h */
int
cyn_query_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key,
	int maxdepth
) {
	int rc;
	unsigned score;
	data_value_t value;
	cynagora_query_t *query;
	struct agent *agent;

	/* get the direct value */
	score = db_test(key, &value);

	/* missing value */
	if (score == 0) {
		default_value(&value);
		on_result_cb(closure, &value);
		return 0;
	}

	/* if not an agent or agent not required */
	agent = required_agent(value.value);
	if (!agent || maxdepth <= 0) {
		on_result_cb(closure, &value);
		return 0;
	}

	/* allocate asynchronous query */
	query = alloc_query(on_result_cb, closure, key, maxdepth);
	if (!query) {
		on_result_cb(closure, &value);
		return -ENOMEM;
	}

	/* call the agent */
	rc = agent->agent_cb(
			agent->name,
			agent->closure,
			&query->key,
			&value.value[agent->len + 1],
			query);
	if (rc < 0)
		cyn_query_reply(query, &value);
	return rc;
}

/* see cyn.h */
int
cyn_test_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
) {
	return cyn_query_async(on_result_cb, closure, key, 0);
}

/* see cyn.h */
int
cyn_check_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
) {
	return cyn_query_async(on_result_cb, closure, key, CYN_SEARCH_DEEP_MAX);
}

/* see cyn.h */
int
cyn_query_subquery_async(
	cynagora_query_t *query,
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
) {
	return cyn_query_async(on_result_cb, closure, key, query->decount - 1);
}

/* see cyn.h */
void
cyn_query_reply(
	cynagora_query_t *query,
	const data_value_t *value
) {
	query->on_result_cb(query->closure, value);
	free(query);
}

/* see cyn.h */
int
cyn_agent_add(
	const char *name,
	agent_cb_t *agent_cb,
	void *closure
) {
	struct agent *agent, **pprev;
	size_t length;

	/* compute and check name */
	length = agent_check_name(name);
	if (!length)
		return -EINVAL;

	/* search the agent */
	agent = search_agent(name, length, &pprev);
	if (agent)
		return -EEXIST;

	/* allocates the memory */
	agent = malloc(sizeof *agent + 1 + length);
	if (!agent)
		return -ENOMEM;

	/* initialize the agent */
	agent->next = 0;
	agent->agent_cb = agent_cb;
	agent->closure = closure;
	agent->len = (uint8_t)length;
	memcpy(agent->name, name, length + 1);
	*pprev = agent;

	return 0;
}

/* see cyn.h */
int
cyn_agent_remove_by_name(
	const char *name
) {
	struct agent *agent, **pprev;
	size_t length;

	/* compute and check name length */
	length = agent_check_name(name);
	if (!length)
		return -EINVAL;

	/* search the agent */
	agent = search_agent(name, length, &pprev);
	if (!agent)
		return -ENOENT;

	/* remove the found agent */
	*pprev = agent->next;
	free(agent);
	return 0;
}

/* see cyn.h */
void
cyn_agent_remove_by_cc(
	agent_cb_t *agent_cb,
	void *closure
) {
	struct agent *it, **pprev;

	pprev = &agents;
	while((it = *pprev))
		if (it->agent_cb != agent_cb || it->closure != closure)
			pprev = &it->next;
		else {
			*pprev = it->next;
			free(it);
		}
}

/* see cyn.h */
void
cyn_changeid_reset(
) {
	changeid.current = 1;
	changeid.instring = 0;
}

/* see cyn.h */
uint32_t
cyn_changeid(
) {
	return changeid.current;
}

/* see cyn.h */
const char *
cyn_changeid_string(
) {
	/* regenerate the string on need */
	if (changeid.current != changeid.instring) {
		changeid.instring = changeid.current;
		snprintf(changeid.string, sizeof changeid.string, "%u", changeid.current);
	}
	/* return the string */
	return changeid.string;
}
