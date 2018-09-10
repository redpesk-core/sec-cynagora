
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include "cache.h"

struct cache
{
	uint32_t begin;
	uint32_t used;
	uint32_t count;
	char content[1];
};

static
uint32_t
lenat(
	cache_t *cache,
	uint32_t pos
) {
	uint32_t p, n;
	char c;

	p = pos + 1;
	if (p >= cache->count)
		p -= cache->count;
	n = 4;
	while (n--) {
		do {
			c = cache->content[p++];
			if (p >= cache->count)
				p -= cache->count;
		} while(c);
	}
	return (p > pos ? p : (p + cache->count)) - pos;
}

static
void
drop_one(
	cache_t *cache
) {
	uint32_t l = lenat(cache, cache->begin);
	cache->used -= l;
	cache->begin += l;
	if (cache->begin > cache->count)
		cache->begin -= cache->count;
}

static
void
addc(
	cache_t *cache,
	char c
) {
	uint32_t pos;
	if (cache->used == cache->count)
		drop_one(cache);
	pos = cache->begin + cache->used++;
	if (pos > cache->count)
		pos -= cache->count;
	cache->content[pos < cache->count ? pos : pos - cache->count] = c;
}

static
void
adds(
	cache_t *cache,
	const char *s
) {
	do { addc(cache, *s); } while(*s++);
}

int
cache_put(
	cache_t *cache,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	int value
) {
	if (cache == NULL
	 || strlen(client) + strlen(session)
		 + strlen(user) + strlen(permission)
		 + 5 > cache->count)
		return -EINVAL;

	addc(cache, (char)value);
	adds(cache, client);
	adds(cache, session);
	adds(cache, user);
	adds(cache, permission);
	return 0;
}

int
cache_search(
	cache_t *cache,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	return -ENOENT;
}

void
cache_clear(
	cache_t *cache
) {
	if (cache) {
		cache->used = 0;
		cache->begin = 0;
	}
}

int
cache_resize(
	cache_t **cache,
	uint32_t newsize
) {
	cache_t *c = *cache, *nc;

	while (c && c->used > newsize)
		drop_one(c);

	nc = malloc(newsize - 1 + sizeof *c);
	if (nc == NULL)
		return -ENOMEM;

	nc->begin = 0;
	nc->count = newsize;
	if (!c || c->used == 0)
		nc->used = 0;
	else {
		if (c->begin + c->used <= c->count)
			memcpy(&nc->content[0], &c->content[c->begin], c->used);
		else {
			memcpy(&nc->content[0], &c->content[c->begin], c->count - c->begin);
			memcpy(&nc->content[c->count - c->begin], &c->content[0], c->used + c->begin - c->count);
		}

		nc->used = c->used;
	}
	*cache = nc;
	free(c);
	return 0;
}

int
cache_create(
	cache_t **cache,
	uint32_t size
) {
	*cache = NULL;
	return cache_resize(cache, size);
}



