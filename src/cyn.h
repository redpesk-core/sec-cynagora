
#pragma once

#define DENY    0
#define ALLOW   1
#define ASK     2
#define DEFAULT DENY

extern
int
cyn_init(
);

/** enter critical recoverable section */
extern
int
cyn_enter(
	const void *magic
);

/** leave critical recoverable section */
extern
int
cyn_leave(
	const void *magic,
	bool commit
);

extern
int
cyn_set(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	uint32_t value
);

extern
int
cyn_drop(
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
cyn_test(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	uint32_t *value
);

extern
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
);

extern
int
cyn_check_async(
	void (*check_cb)(void *closure, uint32_t value),
	void *closure,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
cyn_enter_async(
	void (*enter_cb)(void *closure),
	void *closure
);

extern
int
cyn_enter_async_cancel(
	void (*enter_cb)(void *closure),
	void *closure
);

extern
int
cyn_on_change_add(
	void (*on_change_cb)(void *closure),
	void *closure
);

extern
int
cyn_on_change_remove(
	void (*on_change_cb)(void *closure),
	void *closure
);

