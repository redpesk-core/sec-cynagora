#pragma once

struct cache;
typedef struct cache cache_t;

extern
int
cache_search(
	cache_t *cache,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
cache_put(
	cache_t *cache,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	int value
);

extern
void
cache_clear(
	cache_t *cache
);

extern
int
cache_resize(
	cache_t **cache,
	uint32_t newsize
);

extern
int
cache_create(
	cache_t **cache,
	uint32_t size
);



