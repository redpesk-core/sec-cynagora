#include "rcyn-protocol.h"

const char
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

#if !defined(RCYN_DEFAULT_CHECK_SOCKET_SPEC)
# define RCYN_DEFAULT_CHECK_SOCKET_SPEC "unix:/run/platform/cynara.check"
#endif
#if !defined(RCYN_DEFAULT_ADMIN_SOCKET_SPEC)
# define RCYN_DEFAULT_ADMIN_SOCKET_SPEC "unix:/run/platform/cynara.admin"
#endif
const char
	rcyn_default_check_socket_spec[] = RCYN_DEFAULT_CHECK_SOCKET_SPEC,
	rcyn_default_admin_socket_spec[] = RCYN_DEFAULT_ADMIN_SOCKET_SPEC;
