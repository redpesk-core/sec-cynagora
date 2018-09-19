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


#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>


#include "db.h"
#include "queue.h"
#include "cyn.h"

struct callback
{
	struct callback *next;
	void (*callback)(void *closure);
	void *closure;
};

/** locking critical section */
static const void *lock;
static struct callback *awaiters;
static struct callback *observers;

static
int
delcb(
	void (*callback)(void *closure),
	void *closure,
	struct callback **head
) {
	struct callback *c;

	while((c = *head)) {
		if (c->callback == callback && c->closure == closure) {
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
	void (*callback)(void *closure),
	void *closure,
	struct callback **head
) {
	struct callback *c;

	c = malloc(sizeof *c);
	if (c == NULL)
		return -(errno = ENOMEM);
	c->callback = callback;
	c->closure = closure;
	c->next = *head;
	*head = c;
	return 0;
}

static
int
changed(
) {
	int rc;
	struct callback *c;

	db_cleanup(0);
	rc = db_sync();
	for (c = observers; c ; c = c->next)
		c->callback(c->closure);
	return rc;
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
	void (*enter_cb)(void *closure),
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
	void (*enter_cb)(void *closure),
	void *closure
) {
	return delcb(enter_cb, closure, &awaiters);
}

int
cyn_on_change_add(
	void (*on_change_cb)(void *closure),
	void *closure
) {
	return addcb(on_change_cb, closure, &observers);
}


int
cyn_on_change_remove(
	void (*on_change_cb)(void *closure),
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
	int rc;
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
		rc = queue_play();
		if (rc < 0)
			db_recover();
		else {
			rc = db_backup();
			changed();
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
		e->callback(e->closure);
		free(e);
	}

	return rc;
}

int
cyn_set(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char *value,
	time_t expire
) {
	if (!lock)
		return -EPERM;
	return queue_set(client, session, user, permission, value, expire);
}

int
cyn_drop(
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	if (!lock)
		return -EPERM;
	return queue_drop(client, session, user, permission);
}

void
cyn_list(
	void *closure,
	void (*callback)(
		void *closure,
		const char *client,
		const char *session,
		const char *user,
		const char *permission,
		const char *value,
		time_t expire),
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	db_for_all(closure, callback, client, session, user, permission);
}

int
cyn_test(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char **value,
	time_t *expire
) {
	int rc;

	rc = db_test(client, session, user, permission, value, expire);
	if (rc <= 0)
		*value = DEFAULT;
	else
		rc = 0;
	return rc;
}

int
cyn_check_async(
	void (*check_cb)(void *closure, const char *value, time_t expire),
	void *closure,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	const char *value;
	time_t expire;

	cyn_test(client, session, user, permission, &value, &expire);
	if (!strcmp(value, ALLOW) || !strcmp(value, DENY)) {
		check_cb(closure, value, expire);
		return 0;
	}

	/* TODO: try to resolve AGENT?? */

	check_cb(closure, value, expire);
	return 0;
}

