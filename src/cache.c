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


#include <stdlib.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include "cache.h"

/**
 * A cache item header
 *
 * Each item is followed by values in that given order:
 *  - client: zero  terminated string
 *  - session: zero  terminated string
 *  - user: zero  terminated string
 *  - permission: zero  terminated string
 *  - 
 */
struct item
{
	/** expiration */
	time_t expire;

	/** length of the cache entry including this header */
	uint16_t length;

	/** hit indicator */
	uint8_t hit;

	/** value to store */
	int8_t value;

	/** fake ending character */
	char strings;
};
typedef struct item item_t;

/**
 * The cache structure is a blob of memory ('content')
 * of 'count' bytes of only 'used' bytes.
 * That blob containts at sequence of records of variable length
 */
struct cache
{
	uint32_t used;
	uint32_t count;
	uint8_t content[1];
};

static
inline
item_t *
itemat(
	cache_t *cache,
	uint32_t pos
) {
	return (item_t*)(&cache->content[pos]);
}

static
void
drop_at(
	cache_t *cache,
	uint32_t pos
) {
	uint32_t e, l;

	l = itemat(cache, pos)->length;
	e = pos + l;
	cache->used -= l;
	if (cache->used > e)
		memmove(&cache->content[pos], &cache->content[e], cache->used - e);
}

static
void
drop_lre(
	cache_t *cache
) {
	uint32_t found = 0, iter = 0;
	uint8_t hmin = 255, hit;
	item_t *item;

	while (iter < cache->used) {
		item = itemat(cache, iter);
		hit = item->hit;
		if (hit < hmin)
			found = iter;
		iter += item->length;
	}
	if (found < cache->used)
		drop_at(cache, found);
}

static
void
hit(
	cache_t *cache,
	item_t *target
) {
	uint32_t iter = 0;
	uint8_t hit;
	item_t *item;

	while (iter < cache->used) {
		item = itemat(cache, iter);
		if (item == target)
			hit = 255;
		else {
			hit = item->hit;
			if (hit)
				hit--;
		}
		item->hit = hit;
		iter += item->length;
	}
}

static
const char*
cmpi(
	const char *head,
	const char *other
) {
	char c;
	while(toupper(c = *head++) == toupper(*other++))
		if (!c)
			return head;
	return 0;
}

static
const char*
cmp(
	const char *head,
	const char *other
) {
	char c;
	while((c = *head++) == *other++)
		if (!c)
			return head;
	return 0;
}

static
int
match(
	const char *head,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	head = cmp(head, client);
	if (head)
		head = cmp(head, session);
	if (head)
		head = cmp(head, user);
	if (head)
		head = cmpi(head, permission);
	return !!head;
}

static
item_t*
search(
	cache_t *cache,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	time_t now;
	item_t *item, *found;
	uint32_t iter;

	found = NULL;
	now = time(NULL);
	iter = 0;
	while (iter < cache->used) {
		item = itemat(cache, iter);
		if (item->expire && item->expire < now)
			drop_at(cache, iter);
		else {
			if (match(&item->strings, client, session, user, permission))
				found = item;
			iter += item->length;
		}
	}
	return found;
}

int
cache_put(
	cache_t *cache,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	int value,
	time_t expire
) {
	uint16_t length;
	item_t *item;
	size_t size;

	if (cache == NULL || value < -128 || value > 127)
		return -EINVAL;

	item = search(cache, client, session, user, permission);
	if (item == NULL) {
		/* create an item */
		size = (size_t)(&((item_t*)0)->strings)
			+ strlen(client)
			+ strlen(session)
			+ strlen(user)
			+ strlen(permission);
		size = (size + alignof(item_t) - 1) & ~(alignof(item_t) - 1);
		if (size > 65535)
			return -EINVAL;
		length = (uint16_t)size;
		if (length > cache->count)
			return -ENOMEM;
		while(cache->used + length > cache->count)
			drop_lre(cache);
		item = itemat(cache, cache->used);
		item->length = length;
		stpcpy(1 + stpcpy(1 + stpcpy(1 + stpcpy(&item->strings, client), session), user), permission);
		cache->used += (uint32_t)size;
	}
	item->expire = expire;
	item->hit = 255;
	item->value = (int8_t)value;
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
	item_t *item;

	item = search(cache, client, session, user, permission);
	if (item) {
		hit(cache, item);
		return (int)item->value;
	}
	return -ENOENT;
}

void
cache_clear(
	cache_t *cache
) {
	if (cache)
		cache->used = 0;
}

int
cache_resize(
	cache_t **cache,
	uint32_t newsize
) {
	cache_t *c = *cache, *nc;

	if (c)
		while (c->used > newsize)
			drop_lre(c);

	nc = realloc(c, newsize - 1 + sizeof *c);
	if (nc == NULL)
		return -ENOMEM;

	nc->count = newsize;
	if (!c)
		nc->used = 0;
	*cache = nc;
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



