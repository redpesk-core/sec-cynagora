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
/* IMPLEMENTATION OF CLIENT PART OF CYNAGORA-PROTOCOL                         */
/******************************************************************************/
/******************************************************************************/

typedef struct cynagora cynagora_t;
typedef enum cynagora_type cynagora_type_t;
typedef struct cynagora_key cynagora_key_t;
typedef struct cynagora_value cynagora_value_t;

enum cynagora_type {
	cynagora_Check,
	cynagora_Admin,
	cynagora_Agent
};

struct cynagora_key {
	const char *client;
	const char *session;
	const char *user;
	const char *permission;
};

struct cynagora_value {
	const char *value;
	time_t expire;
};

extern
int
cynagora_open(
	cynagora_t **cynagora,
	cynagora_type_t type,
	uint32_t cache_size,
	const char *socketspec
);

extern
void
cynagora_disconnect(
	cynagora_t *cynagora
);

extern
void
cynagora_close(
	cynagora_t *cynagora
);

extern
int
cynagora_enter(
	cynagora_t *cynagora
);

extern
int
cynagora_leave(
	cynagora_t *cynagora,
	bool commit
);

extern
int
cynagora_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key
);

extern
int
cynagora_test(
	cynagora_t *cynagora,
	const cynagora_key_t *key
);

extern
int
cynagora_set(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	const cynagora_value_t *value
);

extern
int
cynagora_get(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	void (*callback)(
		void *closure,
		const cynagora_key_t *key,
		const cynagora_value_t *value
	),
	void *closure
);

extern
int
cynagora_log(
	cynagora_t *cynagora,
	int on,
	int off
);

extern
int
cynagora_drop(
	cynagora_t *cynagora,
	const cynagora_key_t *key
);

extern
void
cynagora_cache_clear(
	cynagora_t *cynagora
);

extern
int
cynagora_cache_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key
);

extern
int
cynagora_cache_resize(
	cynagora_t *cynagora,
	uint32_t size
);

typedef int (*cynagora_async_ctl_t)(
			void *closure,
			int op,
			int fd,
			uint32_t events);

extern
int
cynagora_async_setup(
	cynagora_t *cynagora,
	cynagora_async_ctl_t controlcb,
	void *closure
);

extern
int
cynagora_async_process(
	cynagora_t *cynagora
);

extern
int
cynagora_async_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	int simple,
	void (*callback)(
		void *closure,
		int status),
	void *closure
);
