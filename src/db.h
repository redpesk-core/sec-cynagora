#pragma once

#define MAX_NAME_LENGTH 32767

/** open the database for files 'names' and 'rules' (can be NULL) */
extern
int
db_open(
	const char *names,
	const char *rules
);

/** close the database */
extern
void
db_close(
);

/** is the database empty */
extern
bool
db_is_empty(
);

/** sync the database */
extern
int
db_sync(
);

/** enter critical recoverable section */
extern
int
db_enter(
);

/** leave critical recoverable section */
extern
int
db_leave(
	bool commit
);

/** get an index for a name */
extern
int
db_get_name_index(
	uint32_t *index,
	const char *name,
	bool needed
);

/** enumerate */
extern
void
db_for_all(
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

/** erase rules */
extern
int
db_drop(
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

/** set rules */
extern
int
db_set(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	uint32_t value
);

/** check rules */
extern
int
db_test(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	uint32_t *value
);

