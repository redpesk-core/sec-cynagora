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

#if !CYN_SEARCH_DEEP_MAX
# define CYN_SEARCH_DEEP_MAX 10
#endif

/**
 * items of the list of observers or awaiters
 */
struct callback
{
	/** link to the next item of the list */
	struct callback *next;
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

	/** length of the name */
	uint8_t len;

	/** name of the agent */
	char name[];
};

/**
 * structure handling an asynchronous check
 */
struct cyn_query
{
	/** callback for handling result of the check */
	on_result_cb_t *on_result_cb;

	/** closure of the callback */
	void *closure;

	/** key of the check */
	data_key_t key;

	/** value of the check */
	data_value_t value;

	/** down counter for recursivity limitation */
	int decount;
};

/** locking critical section */
static const void *lock;

/** head of the list of critical section awaiters */
static struct callback *awaiters;

/** head of the list of change observers */
static struct callback *observers;

/** head of the list of agents */
static struct agent *agents;

/** last changeid */
static uint32_t last_changeid;

/** changeid of the current 'changeid_string' */
static uint32_t last_changeid_string;

/** string buffer for changeid */
static char changeid_string[12];

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

	++last_changeid;
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
	if (lock)
		return -EBUSY;
	lock = magic;
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
	if (lock)
		return addcb(enter_cb, magic, &awaiters);

	lock = magic;
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
	if (!lock)
		return -EALREADY;
	if (lock != magic)
		return -EPERM;

	lock = &lock;
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
		lock = 0;
	else {
		/* the one to awake is at the end of the list */
		p = &awaiters;
		while(e->next) {
			p = &e->next;
			e = *p;
		}
		*p = NULL;
		lock = e->closure;
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
	if (!lock)
		return -EPERM;
	return queue_set(key, value);
}

/* see cyn.h */
int
cyn_drop(
	const data_key_t *key
) {
	if (!lock)
		return -EPERM;
	return queue_drop(key);
}

/* see cyn.h */
void
cyn_list(
	void *closure,
	list_cb_t *callback,
	const data_key_t *key
) {
	db_for_all(closure, callback, key);
}

/**
 * initialize value to its default
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
 * @param name of the agent to find (optionally zero terminated)
 * @param len length of the name
 * @param ppprev for catching the pointer referencing the return item
 * @return 0 if not found or the pointer to the item of the found agent
 */
static
struct agent *
search_agent(
	const char *name,
	uint8_t len,
	struct agent ***ppprev
) {
	struct agent *it, **pprev;

	pprev = &agents;
	while((it = *pprev)
	  &&  (len != it->len || memcmp(it->name, name, (size_t)len)))
		pprev = &it->next;
	*ppprev = pprev;
	return it;
}

/**
 * Return the agent required by the value or 0 if no agent is required
 * or if the agent is not found.
 * @param value string where agent is the prefix followed by one colon
 * @return the item of the required agent or 0 when no agent is required
 */
static
struct agent *
required_agent(
	const char *value
) {
	struct agent **pprev;
	uint8_t len;

	for (len = 0 ; len < UINT8_MAX && value[len] ; len++)
		if (value[len] == ':')
			return search_agent(value, len, &pprev);
	return 0;
}

static
void
async_call_agent(
	struct agent *agent,
	cyn_query_t *query,
	const data_value_t *value
) {
	int rc = agent->agent_cb(
			agent->name,
			agent->closure,
			&query->key,
			&value->value[agent->len + 1],
			query);
	if (rc < 0)
		cyn_reply_query(query, value);
}

static
cyn_query_t *
alloc_query(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key,
	int maxdepth
) {
	size_t szcli, szses, szuse, szper;
	cyn_query_t *query;
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
			ptr = mempcpy(ptr, key->permission, szper);
		}
		query->decount = maxdepth;
	}
	return query;
}







int
cyn_query_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key,
	int maxdepth
) {
	int rc;
	data_value_t value;
	cyn_query_t *query;
	struct agent *agent;

	/* get the direct value */
	rc = db_test(key, &value);

	/* on error or missing result */
	if (rc <= 0) {
		default_value(&value);
		on_result_cb(closure, &value);
		return rc;
	}

	/* if not an agent or agent not required */
	agent = required_agent(value.value);
	if (!agent || maxdepth <= 0) {
		on_result_cb(closure, &value);
		return rc;
	}

	/* allocate asynchronous query */
	query = alloc_query(on_result_cb, closure, key, maxdepth);
	if (!query) {
		on_result_cb(closure, &value);
		return -ENOMEM;
	}

	/* call the agent */
	async_call_agent(agent, query, &value);
	return 0;
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

int
cyn_subquery_async(
	cyn_query_t *query,
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
) {
	return cyn_query_async(on_result_cb, closure, key, query->decount - 1);
}

void
cyn_reply_query(
	cyn_query_t *query,
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
	uint8_t len;

	length = strlen(name);
	if (length <= 0 || length > UINT8_MAX)
		return -EINVAL;
	len = (uint8_t)length++;

	agent = search_agent(name, len, &pprev);
	if (agent)
		return -EEXIST;

	agent = malloc(sizeof *agent + length);
	if (!agent)
		return -ENOMEM;

	agent->next = 0;
	agent->agent_cb = agent_cb;
	agent->closure = closure;
	agent->len = len;
	memcpy(agent->name, name, length);
	*pprev = agent;

	return 0;
}

/* see cyn.h */
int
cyn_agent_remove(
	const char *name
) {
	struct agent *agent, **pprev;
	size_t length;
	uint8_t len;

	length = strlen(name);
	if (length <= 0 || length > UINT8_MAX)
		return -EINVAL;
	len = (uint8_t)length;

	agent = search_agent(name, len, &pprev);
	if (!agent)
		return -ENOENT;

	*pprev = agent->next;
	free(agent);
	return 0;
}

/* see cyn.h */
void
cyn_changeid_reset(
) {
	last_changeid = 1;
}

/* see cyn.h */
uint32_t
cyn_changeid(
) {
	return last_changeid;
}

/* see cyn.h */
const char *
cyn_changeid_string(
) {
	/* regenerate the string on need */
	if (last_changeid != last_changeid_string) {
		last_changeid_string = last_changeid;
		snprintf(changeid_string, sizeof changeid_string, "%u", last_changeid);
	}
	return changeid_string;
}
