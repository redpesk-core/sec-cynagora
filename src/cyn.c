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
		agent_cb_t *agent_cb;
	};
	void *closure;
};

struct async_check
{
	struct async_check *next;
	on_result_cb_t *on_result_cb;
	void *closure;
	data_key_t key;
	struct callback *next_agent;
};

/** locking critical section */
static const void *lock;
static struct callback *awaiters;
static struct callback *observers;
static struct callback *agents;
static struct async_check *asynchecks;

static
int
delcb(
	void *callback,
	void *closure,
	struct callback **head,
	struct async_check *achecks
) {
	struct callback *c;

	while((c = *head)) {
		if (c->any_cb == callback && c->closure == closure) {
			while (achecks) {
				if (achecks->next_agent == c)
					achecks->next_agent = c->next;
				achecks = achecks->next;
			}
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
	return delcb(enter_cb, closure, &awaiters, 0);
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
	return delcb(on_change_cb, closure, &observers, 0);
}

/** leave critical recoverable section */
int
cyn_leave(
	const void *magic,
	bool commit
) {
	int rc;
	struct callback *c, *e, **p;

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
		db_backup();
		rc = queue_play();
		if (rc == 0) {
			db_cleanup(0);
			rc = db_sync();
		}
		if (rc < 0) {
			db_recover();
			db_sync();
		} else {
			for (c = observers; c ; c = c->next)
				c->on_change_cb(c->closure);
		}
	}
	queue_clear();

	e = awaiters;
	if (!e)
		lock = 0;
	else {
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

int
cyn_test(
	const data_key_t *key,
	data_value_t *value
) {
	int rc;

	rc = db_test(key, value);
	if (rc <= 0) {
		value->value = DEFAULT;
		value->expire = 0;
	}
	return rc;
}

static
void
async_on_result(
	void *closure,
	const data_value_t *value
) {
	struct async_check *achk = closure, **pac;
	struct callback *agent;
	int rc;
	data_value_t v;

	if (!value) {
		agent = achk->next_agent;
		while (agent) {
			achk->next_agent = agent->next;
			rc = agent->agent_cb(
				agent->closure,
				&achk->key,
				async_on_result,
				closure);
			if (!rc)
				return;
			agent = achk->next_agent;
		}
		v.value = DEFAULT;
		v.expire = 0;
		value = &v;
	}

	achk->on_result_cb(achk->closure, value);
	pac = &asynchecks;
	while (*pac != achk)
		pac = &(*pac)->next;
	*pac = achk->next;
	free(achk);
}


int
cyn_check_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
) {
	data_value_t value;
	size_t szcli, szses, szuse, szper;
	struct async_check *achk;
	void *ptr;

	cyn_test(key, &value);
	if (!strcmp(value.value, ALLOW) || !strcmp(value.value, DENY)) {
		on_result_cb(closure, &value);
		return 0;
	}

	if (!agents) {
		on_result_cb(closure, &value);
		return 0;
	}

	szcli = key->client ? 1 + strlen(key->client) : 0;
	szses = key->session ? 1 + strlen(key->session) : 0;
	szuse = key->user ? 1 + strlen(key->user) : 0;
	szper = key->permission ? 1 + strlen(key->permission) : 0;
	achk = malloc(szcli + szses + szuse + szper + sizeof *achk);
	if (!achk) {
		on_result_cb(closure, &value);
		return -ENOMEM;
	}

	ptr = achk;
	achk->on_result_cb = on_result_cb;
	achk->closure = closure;
	if (!key->client)
		achk->key.client = 0;
	else {
		achk->key.client = ptr;
		memcpy(ptr, key->client, szcli);
		ptr += szcli;
	}
	if (!key->session)
		achk->key.session = 0;
	else {
		achk->key.session = ptr;
		memcpy(ptr, key->session, szses);
		ptr += szses;
	}
	if (!key->user)
		achk->key.user = 0;
	else {
		achk->key.user = ptr;
		memcpy(ptr, key->user, szuse);
		ptr += szuse;
	}
	if (!key->permission)
		achk->key.permission = 0;
	else {
		achk->key.permission = ptr;
		memcpy(ptr, key->permission, szper);
	}
	achk->next_agent = agents;
	achk->next = asynchecks;
	asynchecks = achk;
	async_on_result(achk, 0);
	return 0;
}

int
cyn_agent_add(
	agent_cb_t *agent_cb,
	void *closure
) {
	return addcb(agent_cb, closure, &agents);
}

int
cyn_agent_remove(
	agent_cb_t *agent_cb,
	void *closure
) {
	return delcb(agent_cb, closure, &agents, asynchecks);
}

