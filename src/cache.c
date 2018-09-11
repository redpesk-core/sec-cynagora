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
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "cache.h"

/**
 * The cache structure is a blob of memory ('content')
 * of 'count' bytes of only 'used' bytes.
 * That blob containts at sequence of records of variable length
 * Each records holds the following values in that given order:
 *  - length: 2 bytes unsigned integer LSB first, MSB second
 *  - hit count: 1 byte unsigned integer
 *  - value: 1 byte unsigned integer
 *  - client: zero  terminated string
 *  - session: zero  terminated string
 *  - user: zero  terminated string
 *  - permission: zero  terminated string
 *  - 
 */
struct cache
{
	uint32_t used;
	uint32_t count;
	uint8_t content[1];
};

static
uint32_t
lenat(
	cache_t *cache,
	uint32_t pos
) {
	return ((uint32_t)cache->content[pos]) |  (((uint32_t)cache->content[pos + 1]) << 8);
}

static
void
drop_at(
	cache_t *cache,
	uint32_t pos
) {
	uint32_t e, l;

	l = lenat(cache, pos);
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
	uint8_t hmin = 255, hint;

	while (iter < cache->used) {
		hint = cache->content[iter + 2];
		if (hint < hmin)
			found = iter;
		iter += lenat(cache, iter);
	}
	if (found < cache->used)
		drop_at(cache, found);
}

static
void
hit(
	cache_t *cache,
	uint32_t pos
) {
	uint32_t iter = 0;
	uint8_t hint;

	while (iter < cache->used) {
		if (iter == pos)
			hint = 255;
		else {
			hint = cache->content[iter + 2];
			if (hint)
				hint--;
		}
		cache->content[iter + 2] = hint;
		iter += lenat(cache, iter);
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
uint32_t
search(
	cache_t *cache,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	char *txt;
	uint32_t iter = 0;

	while (iter < cache->used) {
		txt = (char*)&cache->content[iter + 4];
		if (match(txt, client, session, user, permission))
			return iter;
		iter += lenat(cache, iter);
	}
	return iter;
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
	uint32_t pos;
	size_t size, scli, sses, susr, sper;

	if (cache == NULL || value < 0 || value > 255)
		return -EINVAL;

	pos = search(cache, client, session, user, permission);
	if (pos < cache->used)
		cache->content[pos + 3] = (uint8_t)value;
	else {
		scli = strlen(client);
		sses = strlen(session);
		susr = strlen(user);
		sper = strlen(permission);
		size = scli + sses + susr + sper + 8;
		if (size > 65535)
			return -EINVAL;
		if (size > cache->count)
			return -ENOMEM;
		while(cache->used + (uint32_t)size > cache->count)
			drop_lre(cache);
		pos = cache->used;
		cache->content[pos + 0] = (uint8_t)(size & 255);
		cache->content[pos + 1] = (uint8_t)((size >> 8) & 255);
		cache->content[pos + 2] = (uint8_t)255;
		cache->content[pos + 3] = (uint8_t)value;
		stpcpy(1 + stpcpy(1 + stpcpy(1 + stpcpy((char*)&cache->content[pos + 4], client), session), user), permission);
		cache->used += (uint32_t)size;
	}
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
	uint32_t pos;

	pos = search(cache, client, session, user, permission);
	if (pos < cache->used) {
		hit(cache, pos);
		return (int)cache->content[pos + 3];
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



