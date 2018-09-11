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

typedef enum rcyn_type {
	rcyn_Check,
	rcyn_Admin
} rcyn_type_t;

struct rcyn;
typedef struct rcyn rcyn_t;

extern
int
rcyn_open(
	rcyn_t **rcyn,
	rcyn_type_t type,
	uint32_t cache_size
);

extern
void
rcyn_close(
	rcyn_t *rcyn
);

extern
int
rcyn_enter(
	rcyn_t *rcyn
);

extern
int
rcyn_leave(
	rcyn_t *rcyn,
	bool commit
);

extern
int
rcyn_check(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
rcyn_test(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
rcyn_set(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	int value
);

extern
int
rcyn_get(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	void (*callback)(
		void *closure,
		const char *client,
		const char *session,
		const char *user,
		const char *permission,
		uint32_t value
	),
	void *closure
);

extern
int
rcyn_drop(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
void
rcyn_cache_clear(
	rcyn_t *rcyn
);

extern
int
rcyn_cache_check(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

typedef int (*rcyn_async_ctl_t)(
			void *closure,
			int op,
			int fd,
			uint32_t events);

extern
int
rcyn_async_setup(
	rcyn_t *rcyn,
	rcyn_async_ctl_t controlcb,
	void *closure
);

extern
int
rcyn_async_process(
	rcyn_t *rcyn
);

extern
int
rcyn_async_check(
	rcyn_t *rcyn,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	bool test,
	void (*callback)(
		void *closure,
		int status),
	void *closure
);

