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

typedef void (on_enter_cb_t)(void *closure);
typedef void (on_change_cb_t)(void *closure);
typedef void (on_result_cb_t)(void *closure, const data_value_t *value);

typedef void (list_cb_t)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value);

typedef int (agent_cb_t)(
		void *agent_closure,
		const data_key_t *key,
		on_result_cb_t *on_result_cb,
		void *on_result_closure);

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
cyn_enter_async(
	on_enter_cb_t *enter_cb,
	void *closure
);

extern
int
cyn_enter_async_cancel(
	on_enter_cb_t *enter_cb,
	void *closure
);

extern
int
cyn_on_change_add(
	on_change_cb_t *on_change_cb,
	void *closure
);

extern
int
cyn_on_change_remove(
	on_change_cb_t *on_change_cb,
	void *closure
);

extern
int
cyn_set(
	const data_key_t *key,
	const data_value_t *value
);

extern
int
cyn_drop(
	const data_key_t *key
);

extern
int
cyn_test(
	const data_key_t *key,
	data_value_t *value
);

extern
void
cyn_list(
	void *closure,
	list_cb_t *callback,
	const data_key_t *key
);

extern
int
cyn_check_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
);

extern
int
cyn_agent_add(
	agent_cb_t *agent_cb,
	void *closure
);

extern
int
cyn_agent_remove(
	agent_cb_t *agent_cb,
	void *closure
);

