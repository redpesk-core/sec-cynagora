/*
 * Copyright (C) 2018-2026 IoT.bzh Company
 * Author: José Bollo <jose.bollo@iot.bzh>
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
#pragma once
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF CACHE IN CLIENTS                                         */
/******************************************************************************/
/******************************************************************************/

/** opaque structure for cache */
typedef struct cache cache_t;

/**
 * Search the stored value for the key
 * @param cache the cache handler
 * @param key the key to search
 * @return the stored value or -ENOENT if not found
 */
extern
int
cache_search(
	cache_t *cache,
	const cynagora_key_t *key
);

/**
 * Add the value for the key in the cache
 * @param cache the cache handler
 * @param key the key to cache
 * @param value the value (must be an integer from -128 to 127)
 * @param expire expiration date
 * @return 0 on success
 *         -EINVAL invalid argument
 *         -ENOMEM too big for the cache size
 */
extern
int
cache_put(
	cache_t *cache,
	const cynagora_key_t *key,
	int value,
	time_t expire,
	bool absolute
);

/**
 * Clear the content of the cache if the cacheid doesn't match the current one
 * @param cache the cache handler
 * @param cacheid the cacheid to set or zero to force clearing
 */
extern
void
cache_clear(
	cache_t *cache,
	uint32_t cacheid
);

/**
 * resize the given cache
 * @param cache pointer to the cache handler
 * @param newsize new size to set to the cache
 * @return 0 on success
 *         -ENOMEM not enough memory
 */
extern
int
cache_resize(
	cache_t **cache,
	uint32_t newsize
);

/**
 * create a cache
 * @param cache pointer to the cache handler
 * @param size size to set to the cache
 * @return 0 on success
 *         -ENOMEM not enough memory
 */
extern
int
cache_create(
	cache_t **cache,
	uint32_t size
);

/**
 * Constants for callback of cache_iterate:
 *  - CACHE_ITER_DROP: drop the cache entrry
 *  - CACHE_ITER_STOP: stop iteration
 */
#define CACHE_ITER_DROP 1
#define CACHE_ITER_STOP 2

/**
 * Iterate over cache entries and report it
 * @param cache the cache to inspect
 * @param callback the function to call for each entries
 * @param closure a closure for the callback
 */
extern
void
cache_iterate(
	cache_t *cache,
	int (*callback)(void *closure, const cynagora_key_t *key, int value, time_t expire, int hit),
	void *closure
);
