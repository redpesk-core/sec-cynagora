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
	int rc = db_sync();
	struct callback *c;

	for (c = observers; c ; c = c->next)
		c->callback(c->closure);
	return rc;
}

int
cyn_init(
) {
	/* TODO: paths? */
	int rc = db_open("/var/lib/cynara/cynara.names", "/var/lib/cynara/cynara.rules");
	if (rc == 0 && db_is_empty()) {
		/* TODO: init? */
		rc = db_set("System", "*", "*", "*", 1);
		db_sync();
	}
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
	if (commit)
		rc = queue_play() ?: changed();
	else
		rc = 0;
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
	uint32_t value
) {
	if (lock && lock != &lock)
		return queue_set(client, session, user, permission, value);

	return db_set(client, session, user, permission, value) ?: changed();
}

int
cyn_drop(
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	if (lock && lock != &lock)
		return queue_drop(client, session, user, permission);

	return db_drop(client, session, user, permission) ?: changed();
}

int
cyn_test(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	uint32_t *value
) {
	int rc;

	rc = db_test(client, session, user, permission, value);
	if (rc <= 0)
		*value = DEFAULT;
	else
		rc = 0;
	return rc;
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
		uint32_t value),
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	db_for_all(closure, callback, client, session, user, permission);
}

int
cyn_check_async(
	void (*check_cb)(void *closure, uint32_t value),
	void *closure,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	uint32_t value;

	cyn_test(client, session, user, permission, &value);
	if (value == ALLOW || value == DENY) {
		check_cb(closure, value);
		return 0;
	}

	/* TODO: try to resolve AGENT?? */

	check_cb(closure, value);
	return 0;
}

