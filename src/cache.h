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

struct cache;
typedef struct cache cache_t;

extern
int
cache_search(
	cache_t *cache,
	const rcyn_key_t *key
);

extern
int
cache_put(
	cache_t *cache,
	const rcyn_key_t *key,
	int value,
	time_t expire
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



