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
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF CACHE IN CLIENTS                                         */
/******************************************************************************/
/******************************************************************************/

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdalign.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>

#include "cynagora.h"
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
	char strings[];
};
typedef struct item item_t;

/**
 * The cache structure is a blob of memory ('content')
 * of 'count' bytes of only 'used' bytes.
 * That blob contains at sequence of records of variable length
 */
struct cache
{
	/** used for clearing */
	uint32_t cacheid;

	/** count of bytes used */
	uint32_t used;

	/** count of bytes allocated */
	uint32_t count;

	/** content of the cache */
	uint8_t content[];
};

/**
 * return the item at a given position
 * @param cache the cache
 * @param pos the position of the item
 * @return the item
 */
static
inline
item_t *
itemat(
	cache_t *cache,
	uint32_t pos
) {
	return (item_t*)(&cache->content[pos]);
}

/**
 * Removes the item at position pos
 * @param cache the cache
 * @param pos the position of the item to remove
 */
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
	if (cache->used > pos)
		memmove(&cache->content[pos], &cache->content[e], cache->used - pos);
}

/**
 * Removes the oldest hit target
 * @param cache the cache
 */
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
		if (hit <= hmin) {
			found = iter;
			hmin = hit;
		}
		iter += item->length;
	}
	if (found < cache->used)
		drop_at(cache, found);
}

/**
 * tells the target is used
 * @param cache the cache
 * @param target the target to hit
 */
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

/**
 * Compare the head with a string and either return NULL if it doesn't match or
 * otherwise return the pointer to the next string for heading.
 * @param head head of scan
 * @param other string to compare
 * @return NULL if no match or pointer to the strings that follows head if match
 */
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
	return NULL;
}

/**
 * Compare in a case independant method the head with a string and either
 * return NULL if it doesn't match or otherwise return the pointer to the
 * next string for heading.
 * @param head head of scan
 * @param other string to compare
 * @return NULL if no match or pointer to the strings that follows head if match
 */
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

/**
 * Check if a head of strings matche the key
 * @param head the head of strings
 * @param key the key
 * @return true if matches or false other wise
 */
static
bool
match(
	const char *head,
	const cynagora_key_t *key
) {
	head = cmp(head, key->client);
	if (head) {
		head = cmp(head, key->session);
		if (head) {
			head = cmp(head, key->user);
			if (head) {
				head = cmpi(head, key->permission);
				if (head)
					return true;
			}
		}
	}
	return false;
}

/**
 * Search the item matching key and return it. Also remove expired entries
 * @param cache the cache
 * @param key the key to search
 * @return the found item or NULL if not found
 */
static
item_t*
search(
	cache_t *cache,
	const cynagora_key_t *key
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
			if (match(item->strings, key))
				found = item;
			iter += item->length;
		}
	}
	return found;
}

/* see cache.h */
int
cache_put(cache_t *cache,
	const cynagora_key_t *key,
	int value,
	time_t expire,
	bool absolute
) {
	uint16_t length;
	item_t *item;
	size_t size;

	if (cache == NULL || value < -128 || value > 127 || expire < 0)
		return -EINVAL;

	item = search(cache, key);
	if (item == NULL) {
		/* create an item */
		size = sizeof *item
			+ strlen(key->client)
			+ strlen(key->session)
			+ strlen(key->user)
			+ strlen(key->permission);
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
		stpcpy(1 + stpcpy(1 + stpcpy(1 + stpcpy(item->strings, key->client), key->session), key->user), key->permission);
		cache->used += (uint32_t)size;
	}
	item->expire = !expire ? 0 : absolute ? expire : expire + time(NULL);
	item->hit = 255;
	item->value = (int8_t)value;
	return 0;
}

/* see cache.h */
int
cache_search(
	cache_t *cache,
	const cynagora_key_t *key
) {
	item_t *item;

	if (cache) {
		item = search(cache, key);
		if (item) {
			hit(cache, item);
			return (int)item->value;
		}
	}
	return -ENOENT;
}

/* see cache.h */
void
cache_clear(
	cache_t *cache,
	uint32_t cacheid
) {
	if (cache && (cache->cacheid != cacheid || !cacheid)) {
		cache->cacheid = cacheid;
		cache->used = 0;
	}
}

/* see cache.h */
int
cache_resize(
	cache_t **cache,
	uint32_t newsize
) {
	cache_t *oldcache = *cache, *newcache;

	if (newsize == 0) {
		/* erase all */
		free(oldcache);
		newcache = NULL;
	} else {
		/* coerce cache values if downsizing */
		if (oldcache) {
			while (oldcache->used > newsize)
				drop_lre(oldcache);
		}

		/* reallocate the cache */
		newcache = realloc(oldcache, newsize + sizeof *oldcache);
		if (newcache == NULL)
			return -ENOMEM;

		/* init */
		newcache->count = newsize;
		if (!oldcache) {
			newcache->cacheid = 0;
			newcache->used = 0;
		}
	}
	/* update cache */
	*cache = newcache;
	return 0;
}

/* see cache.h */
int
cache_create(
	cache_t **cache,
	uint32_t size
) {
	*cache = NULL;
	return cache_resize(cache, size);
}


