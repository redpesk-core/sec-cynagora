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

struct callback
{
	struct callback *next;
	union {
		void *any_cb;
		on_enter_cb_t *on_enter_cb;
		on_change_cb_t *on_change_cb;
	};
	void *closure;
};

struct agent
{
	struct agent *next;
	agent_cb_t *agent_cb;
	void *closure;
	uint8_t len;
	char name[];
};

/** locking critical section */
static const void *lock;
static struct callback *awaiters;
static struct callback *observers;
static struct agent *agents;
static uint32_t last_changeid;
static uint32_t last_changeid_string;
static char changeid_string[12];

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
			return 1;
		}
		head = &c->next;
	}
	return 0;
}

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
		return -(errno = ENOMEM);
	c->any_cb = callback;
	c->closure = closure;
	c->next = *head;
	*head = c;
	return 0;
}

static
void
changed(
) {
	struct callback *c;

	++last_changeid;
	for (c = observers; c ; c = c->next)
		c->on_change_cb(c->closure);
}

/** enter critical recoverable section */
int
cyn_enter(
	const void *magic
) {
	if (lock)
		return -EBUSY;
	lock = magic;
	return 0;
}

int
cyn_enter_async(
	on_enter_cb_t *enter_cb,
	void *closure
) {
	if (lock)
		return addcb(enter_cb, closure, &awaiters);

	lock = closure;
	enter_cb(closure);
	return 0;
}

int
cyn_enter_async_cancel(
	on_enter_cb_t *enter_cb,
	void *closure
) {
	return delcb(enter_cb, closure, &awaiters);
}

int
cyn_on_change_add(
	on_change_cb_t *on_change_cb,
	void *closure
) {
	return addcb(on_change_cb, closure, &observers);
}

int
cyn_on_change_remove(
	on_change_cb_t *on_change_cb,
	void *closure
) {
	return delcb(on_change_cb, closure, &observers);
}

/** leave critical recoverable section */
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

int
cyn_set(
	const data_key_t *key,
	const data_value_t *value
) {
	if (!lock)
		return -EPERM;
	return queue_set(key, value);
}

int
cyn_drop(
	const data_key_t *key
) {
	if (!lock)
		return -EPERM;
	return queue_drop(key);
}

void
cyn_list(
	void *closure,
	list_cb_t *callback,
	const data_key_t *key
) {
	db_for_all(closure, callback, key);
}

static
data_value_t *
default_value(
	data_value_t *value
) {
	value->value = DEFAULT;
	value->expire = 0;
	return value;
}

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

struct async_check
{
	on_result_cb_t *on_result_cb;
	void *closure;
	data_key_t key;
	data_value_t value;
	int decount;
};

static
void
async_reply(
	struct async_check *achk
) {
	achk->on_result_cb(achk->closure, &achk->value);
	free(achk);
}

static
void
async_on_result(
	void *closure,
	const data_value_t *value
);

static
void
async_call_agent(
	struct agent *agent,
	struct async_check *achk
) {
	int rc = agent->agent_cb(
			agent->name,
			agent->closure,
			&achk->key,
			&achk->value.value[agent->len + 1],
			async_on_result,
			achk);
	if (rc < 0)
		async_reply(achk);
}

static
void
async_on_result(
	void *closure,
	const data_value_t *value
) {
	struct async_check *achk = closure;
	struct agent *agent;

	achk->value = *value;
	agent = required_agent(value->value);
	if (agent && achk->decount) {
		achk->decount--;
		async_call_agent(agent, achk);
	} else
		async_reply(achk);
}

static
int
async_check_or_test(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key,
	int agentdeep
) {
	int rc;
	data_value_t value;
	size_t szcli, szses, szuse, szper;
	struct async_check *achk;
	struct agent *agent;
	void *ptr;

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
	if (!agent || !agentdeep) {
		on_result_cb(closure, &value);
		return rc;
	}

	/* allocate asynchronous query */
	szcli = key->client ? 1 + strlen(key->client) : 0;
	szses = key->session ? 1 + strlen(key->session) : 0;
	szuse = key->user ? 1 + strlen(key->user) : 0;
	szper = key->permission ? 1 + strlen(key->permission) : 0;
	achk = malloc(szcli + szses + szuse + szper + sizeof *achk);
	if (!achk) {
		on_result_cb(closure, &value);
		return -ENOMEM;
	}

	/* init the structure */
	ptr = &achk[1];
	achk->on_result_cb = on_result_cb;
	achk->closure = closure;
	if (!key->client)
		achk->key.client = 0;
	else {
		achk->key.client = ptr;
		ptr = mempcpy(ptr, key->client, szcli);
	}
	if (!key->session)
		achk->key.session = 0;
	else {
		achk->key.session = ptr;
		ptr = mempcpy(ptr, key->session, szses);
	}
	if (!key->user)
		achk->key.user = 0;
	else {
		achk->key.user = ptr;
		ptr = mempcpy(ptr, key->user, szuse);
	}
	if (!key->permission)
		achk->key.permission = 0;
	else {
		achk->key.permission = ptr;
		ptr = mempcpy(ptr, key->permission, szper);
	}
	achk->value = value;
	achk->decount = agentdeep;

	/* call the agent */
	async_call_agent(agent, achk);
	return 0;
}

int
cyn_test_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
) {
	return async_check_or_test(on_result_cb, closure, key, 0);
}

int
cyn_check_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
) {
	return async_check_or_test(on_result_cb, closure, key, 10);
}

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

void
cyn_changeid_reset(
) {
	last_changeid = 1;
}

uint32_t
cyn_changeid(
) {
	return last_changeid;
}

extern
const char *
cyn_changeid_string(
) {
	if (last_changeid != last_changeid_string) {
		last_changeid_string = last_changeid;
		snprintf(changeid_string, sizeof changeid_string, "%u", last_changeid);
	}
	return changeid_string;
}