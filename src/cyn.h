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

#define DENY    "no"
#define ALLOW   "yes"
#define ASK     "ask"
#define DEFAULT DENY

/** enter critical recoverable section */
extern
int
cyn_enter(
	const void *magic
);

/** leave critical recoverable section */
extern
int
cyn_leave(
	const void *magic,
	bool commit
);

extern
int
cyn_set(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char *value,
	time_t expire
);

extern
int
cyn_drop(
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
cyn_test(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char **value,
	time_t *expire
);

extern
void
cyn_list(
	void *closure,
	void (*callback)(
		void *closure,
		const char *client,
		const char *session,
		const char *user,
		const char *permission,
		const char *value,
		time_t expire),
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
cyn_check_async(
	void (*check_cb)(void *closure, const char *value, time_t expire),
	void *closure,
	const char *client,
	const char *session,
	const char *user,
	const char *permission
);

extern
int
cyn_enter_async(
	void (*enter_cb)(void *closure),
	void *closure
);

extern
int
cyn_enter_async_cancel(
	void (*enter_cb)(void *closure),
	void *closure
);

extern
int
cyn_on_change_add(
	void (*on_change_cb)(void *closure),
	void *closure
);

extern
int
cyn_on_change_remove(
	void (*on_change_cb)(void *closure),
	void *closure
);

