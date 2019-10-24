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
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF COMMON PROTOCOL VALUES, CONSTANTS, PROCESSES             */
/******************************************************************************/
/******************************************************************************/

#include <stdlib.h>

#include "cyn-protocol.h"

const char
	_ack_[] = "ack",
	_agent_[] = "agent",
	_ask_[] = "ask",
	_check_[] = "check",
	_clear_[] = "clear",
	_commit_[] = "commit",
	_cynagora_[] = "cynagora",
	_done_[] = "done",
	_drop_[] = "drop",
	_enter_[] = "enter",
	_error_[] = "error",
	_get_[] = "get",
	_item_[] = "item",
	_leave_[] = "leave",
	_log_[] = "log",
	_no_[] = "no",
	_off_[] = "off",
	_on_[] = "on",
	_reply_[] = "reply",
	_rollback_[] = "rollback",
	_set_[] = "set",
	_sub_[] = "sub",
	_test_[] = "test",
	_yes_[] = "yes";


#if !defined(DEFAULT_SOCKET_SCHEME)
#    define  DEFAULT_SOCKET_SCHEME  "unix"
#endif

#if !defined(DEFAULT_SOCKET_DIR)
#    define  DEFAULT_SOCKET_DIR  "/var/run/cynagora"
#endif

#define  DEF_PREFIX  DEFAULT_SOCKET_SCHEME":"DEFAULT_SOCKET_DIR"/"

#if !defined(DEFAULT_CHECK_SOCKET_BASE)
# define DEFAULT_CHECK_SOCKET_BASE "cynagora.check"
#endif
#if !defined(DEFAULT_ADMIN_SOCKET_BASE)
# define DEFAULT_ADMIN_SOCKET_BASE "cynagora.admin"
#endif
#if !defined(DEFAULT_AGENT_SOCKET_BASE)
# define DEFAULT_AGENT_SOCKET_BASE "cynagora.agent"
#endif


#if !defined(DEFAULT_CHECK_SOCKET_SPEC)
# define DEFAULT_CHECK_SOCKET_SPEC   DEF_PREFIX DEFAULT_CHECK_SOCKET_BASE
#endif
#if !defined(DEFAULT_ADMIN_SOCKET_SPEC)
# define DEFAULT_ADMIN_SOCKET_SPEC   DEF_PREFIX DEFAULT_ADMIN_SOCKET_BASE
#endif
#if !defined(DEFAULT_AGENT_SOCKET_SPEC)
# define DEFAULT_AGENT_SOCKET_SPEC   DEF_PREFIX DEFAULT_AGENT_SOCKET_BASE
#endif

const char
	cyn_default_socket_scheme[] = DEFAULT_SOCKET_SCHEME,
	cyn_default_socket_dir[] = DEFAULT_SOCKET_DIR,
	cyn_default_check_socket_base[] = DEFAULT_CHECK_SOCKET_BASE,
	cyn_default_admin_socket_base[] = DEFAULT_ADMIN_SOCKET_BASE,
	cyn_default_agent_socket_base[] = DEFAULT_AGENT_SOCKET_BASE,
	cyn_default_check_socket_spec[] = DEFAULT_CHECK_SOCKET_SPEC,
	cyn_default_admin_socket_spec[] = DEFAULT_ADMIN_SOCKET_SPEC,
	cyn_default_agent_socket_spec[] = DEFAULT_AGENT_SOCKET_SPEC;

/* see cynagora-protocol.h */
const char *
cyn_get_socket_check(
	const char *value
) {
	return value
		?: secure_getenv("CYNAGORA_SOCKET_CHECK")
		?: cyn_default_check_socket_spec;
}

/* see cynagora-protocol.h */
const char *
cyn_get_socket_admin(
	const char *value
) {
	return value
		?: secure_getenv("CYNAGORA_SOCKET_ADMIN")
		?: cyn_default_admin_socket_spec;
}

/* see cynagora-protocol.h */
const char *
cyn_get_socket_agent(
	const char *value
) {
	return value
		?: secure_getenv("CYNAGORA_SOCKET_AGENT")
		?: cyn_default_agent_socket_spec;
}
