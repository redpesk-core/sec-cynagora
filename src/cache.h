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
	time_t expire
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
 *
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
 *
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
