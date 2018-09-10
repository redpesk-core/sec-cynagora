
extern
int
queue_drop(
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
queue_set(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	uint32_t value
);

extern
void
queue_clear(
);

extern
int
queue_play(
);


