/*
 * Copyright (C) 2018-2022 IoT.bzh Company
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
/* IMPLEMENTATION OF COMMON PROTOCOL VALUES, CONSTANTS, PROCESSES             */
/******************************************************************************/
/******************************************************************************/

/* predefined protocol strings */
extern const char
	_ack_[],
	_agent_[],
	_ask_[],
	_check_[],
	_clearall_[],
	_clear_[],
	_commit_[],
	_cynagora_[],
	_done_[],
	_drop_[],
	_enter_[],
	_error_[],
	_get_[],
	_item_[],
	_leave_[],
	_log_[],
	_no_[],
	_off_[],
	_on_[],
	_reply_[],
	_rollback_[],
	_set_[],
	_sub_[],
	_test_[],
	_yes_[];

/* predefined names */
extern const char
	cyn_default_socket_scheme[],
	cyn_default_socket_dir[],
	cyn_default_check_socket_base[],
	cyn_default_admin_socket_base[],
	cyn_default_agent_socket_base[],
	cyn_default_check_socket_spec[],
	cyn_default_admin_socket_spec[],
	cyn_default_agent_socket_spec[];

/**
 * Get the socket specification for check usage
 *
 * @param value some value or NULL for getting default
 * @return the socket specification for check usage
 */
extern
const char *
cyn_get_socket_check(
	const char *value
);

/**
 * Get the socket specification for admin usage
 *
 * @param value some value or NULL for getting default
 * @return the socket specification for admin usage
 */
extern
const char *
cyn_get_socket_admin(
	const char *value
);

/**
 * Get the socket specification for agent usage
 *
 * @param value some value or NULL for getting default
 * @return the socket specification for agent usage
 */
extern
const char *
cyn_get_socket_agent(
	const char *value
);
