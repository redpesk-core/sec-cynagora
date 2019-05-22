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

#include <stdlib.h>

#include "rcyn-protocol.h"

const char
	_agent_[] = "agent",
	_check_[] = "check",
	_drop_[] = "drop",
	_enter_[] = "enter",
	_get_[] = "get",
	_leave_[] = "leave",
	_rcyn_[] = "rcyn",
	_set_[] = "set",
	_test_[] = "test";

const char
	_commit_[] = "commit",
	_rollback_[] = "rollback";

const char
	_clear_[] = "clear",
	_done_[] = "done",
	_error_[] = "error",
	_item_[] = "item",
	_no_[] = "no",
	_yes_[] = "yes";

#if !defined(RCYN_DEFAULT_SOCKET_SCHEME)
#    define  RCYN_DEFAULT_SOCKET_SCHEME  "unix"
#endif

#if !defined(RCYN_DEFAULT_SOCKET_DIR)
#    define  RCYN_DEFAULT_SOCKET_DIR  "/var/run/cynara"
#endif

#define  DEF_PREFIX  RCYN_DEFAULT_SOCKET_SCHEME":"RCYN_DEFAULT_SOCKET_DIR"/"

#if !defined(RCYN_DEFAULT_CHECK_SOCKET_BASE)
# define RCYN_DEFAULT_CHECK_SOCKET_BASE "cynara.check"
#endif
#if !defined(RCYN_DEFAULT_ADMIN_SOCKET_BASE)
# define RCYN_DEFAULT_ADMIN_SOCKET_BASE "cynara.admin"
#endif
#if !defined(RCYN_DEFAULT_AGENT_SOCKET_BASE)
# define RCYN_DEFAULT_AGENT_SOCKET_BASE "cynara.agent"
#endif


#if !defined(RCYN_DEFAULT_CHECK_SOCKET_SPEC)
# define RCYN_DEFAULT_CHECK_SOCKET_SPEC   DEF_PREFIX RCYN_DEFAULT_CHECK_SOCKET_BASE
#endif
#if !defined(RCYN_DEFAULT_ADMIN_SOCKET_SPEC)
# define RCYN_DEFAULT_ADMIN_SOCKET_SPEC   DEF_PREFIX RCYN_DEFAULT_ADMIN_SOCKET_BASE
#endif
#if !defined(RCYN_DEFAULT_AGENT_SOCKET_SPEC)
# define RCYN_DEFAULT_AGENT_SOCKET_SPEC   DEF_PREFIX RCYN_DEFAULT_AGENT_SOCKET_BASE
#endif

const char
	rcyn_default_socket_scheme[] = RCYN_DEFAULT_SOCKET_SCHEME,
	rcyn_default_socket_dir[] = RCYN_DEFAULT_SOCKET_DIR,
	rcyn_default_check_socket_base[] = RCYN_DEFAULT_CHECK_SOCKET_BASE,
	rcyn_default_admin_socket_base[] = RCYN_DEFAULT_ADMIN_SOCKET_BASE,
	rcyn_default_agent_socket_base[] = RCYN_DEFAULT_AGENT_SOCKET_BASE,
	rcyn_default_check_socket_spec[] = RCYN_DEFAULT_CHECK_SOCKET_SPEC,
	rcyn_default_admin_socket_spec[] = RCYN_DEFAULT_ADMIN_SOCKET_SPEC,
	rcyn_default_agent_socket_spec[] = RCYN_DEFAULT_AGENT_SOCKET_SPEC;

const char *
rcyn_get_socket_check(
	const char *value
) {
	return value
		?: secure_getenv("CYNARA_SOCKET_CHECK")
		?: rcyn_default_check_socket_spec;
}

const char *
rcyn_get_socket_admin(
	const char *value
) {
	return value
		?: secure_getenv("CYNARA_SOCKET_ADMIN")
		?: rcyn_default_admin_socket_spec;
}

const char *
rcyn_get_socket_agent(
	const char *value
) {
	return value
		?: secure_getenv("CYNARA_SOCKET_AGENT")
		?: rcyn_default_agent_socket_spec;
}
