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

extern const char
	_agent_[],
	_check_[],
	_drop_[],
	_enter_[],
	_get_[],
	_leave_[],
	_rcyn_[],
	_set_[],
	_test_[];

extern const char
	_commit_[],
	_rollback_[];

extern const char
	_clear_[],
	_done_[],
	_error_[],
	_item_[],
	_no_[],
	_yes_[];

extern const char
	rcyn_default_socket_scheme[],
	rcyn_default_socket_dir[],
	rcyn_default_check_socket_base[],
	rcyn_default_admin_socket_base[],
	rcyn_default_agent_socket_base[],
	rcyn_default_check_socket_spec[],
	rcyn_default_admin_socket_spec[],
	rcyn_default_agent_socket_spec[];

extern
const char *
rcyn_get_socket_check(
	const char *value
);

extern
const char *
rcyn_get_socket_admin(
	const char *value
);

extern
const char *
rcyn_get_socket_agent(
	const char *value
);
