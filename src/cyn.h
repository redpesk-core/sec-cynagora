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
/* IMPLEMENTATION OF LOCAL CYNAGORA API                                       */
/******************************************************************************/
/******************************************************************************/

#define CYN_VERSION 100

/**
 * Callback for entering asynchronousely the critical section
 * When called it receives the 'magic' argument given when
 * 'cyn_enter_async' was called.
 */
typedef void (on_enter_cb_t)(void *magic);

/**
 * Callback for being notified of changes in database
 * When called it receives the 'closure' argument given when
 * 'cyn_on_change_add' was called.
 */
typedef void (on_change_cb_t)(void *closure);

/**
 * Callback for receiving the result of a test or check
 * When called, receives the result 'value' of the request and
 * the 'closure' given when calling asynchronous query function.
 */
typedef void (on_result_cb_t)(void *closure, const data_value_t *value);

/**
 * Callback for listing data of the database.
 * When call receives the 'closure' given when 'cyn_list' is called
 * and the pair 'key', 'value' of the listed item.
 */
typedef void (list_cb_t)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value);

/**
 * Opaque structure for agent subqueries and responses.
 */
typedef struct cynagora_query cynagora_query_t;


/**
 * Callback for querying agents
 */
typedef int (agent_cb_t)(
		const char *name,
		void *agent_closure,
		const data_key_t *key,
		const char *value,
		cynagora_query_t *query);

/**
 * Enter in the critical recoverable section if possible
 *
 * @param magic a pointer not null that must be used in 'cyn_leave'
 * @return 0       entered
 *         -EBUSY  critical section already locked
 *         -EINVAL magic == NULL
 *
 * @see cyn_leave, cyn_enter_async
 */
extern
int
cyn_enter(
	const void *magic
);

/**
 * Leave the entered the critical recoverable section if possible
 *
 * @param magic a pointer not null that must be the one passed to
 *              'cyn_enter' or 'cyn_enter_async'
 * @param commit if true, the changes are committed to database, conversely
 *               if false, the changes made since entering the critical
 *               section are discarded
 * @return 0 success
 *         -EALREADY already unlocked
 *         -EINVAL magic == NULL
 *         -EPERM  magic doesn't match the magic that entered
 *         other negative codes: error when commiting the database
 *
 * @see cyn_enter, cyn_enter_async
 */
extern
int
cyn_leave(
	const void *magic,
	bool commit
);

/**
 * Enter asynchronously in the critical recoverable section if possible.
 * If the critical recoverable section is free, lock it with magic,
 * call the callback 'enter_cb' and returns 0. Otherwise, if the critical
 * section is already taken, the functions enqueue the entering request
 * and the callback will be called when the critical recoverable section is
 * available.
 *
 * @param enter_cb the entering callback
 * @param magic a pointer not null that must be used in 'cyn_leave'
 * @return 0  entered or or queued
 *         -ENOMEM critical section already locked but request can't be queued
 *         -EINVAL magic == NULL
 *
 * @see cyn_leave, cyn_enter, cyn_enter_async_cancel
 */
extern
int
cyn_enter_async(
	on_enter_cb_t *enter_cb,
	void *magic
);

/**
 * Cancel a an asynchonous waiter to enter
 *
 * @param enter_cb the enter callback of the waiter
 * @param magic the closure of the waiter
 * @return 0 if entry found and removed, -ENOENT if not found
 *
 * @see cyn_enter_async
 */
extern
int
cyn_enter_async_cancel(
	on_enter_cb_t *enter_cb,
	void *magic
);

/**
 * Add an observer to the list of observers
 *
 * @param on_change_cb callback receiving change events
 * @param closure closure of the callback
 * @return 0 success
 *         -ENOMEM can't queue the observer
 *
 * @see cyn_on_change_remove
 */
extern
int
cyn_on_change_add(
	on_change_cb_t *on_change_cb,
	void *closure
);

/**
 * Removes an on change observer
 *
 * @param on_change_cb the callback of the observer
 * @param closure the closure of the observer
 * @return 0 if entry found and removed, -ENOENT if not found
 *
 * @see cyn_on_change_add
 */
extern
int
cyn_on_change_remove(
	on_change_cb_t *on_change_cb,
	void *closure
);

/**
 * Set or add the rule key/value to the change list to commit
 *
 * @param key the key of the rule
 * @param value the value of the rule for the key
 * @return 0 on success
 *         -EPERM if not locked in critical recoverable section
 *         other negative codes: error when queuing the change
 */
extern
int
cyn_set(
	const data_key_t *key,
	const data_value_t *value
);

/**
 * Drop any rule matching the key
 *
 * @param key the key of the rule
 * @return 0 on success
 *         -EPERM if not locked in critical recoverable section
 *         other negative codes: error when queuing the change
 */
extern
int
cyn_drop(
	const data_key_t *key
);

/**
 * Enumerate all items matching the key
 *
 * @param callback callback function called for each found item
 * @param closure the closure to the callback
 * @param key the key to select items
 */
extern
void
cyn_list(
	list_cb_t *callback,
	void *closure,
	const data_key_t *key
);

/**
 * Query the value for the given key.
 *
 * Note that the callback can be called during the call to that function.
 *
 * Note also that the callback will always be called either before the
 * function returns or else later.
 *
 * @param on_result_cb callback function receiving the result
 * @param closure closure for the callback
 * @param key key to be queried
 * @param maxdepth maximum imbrication of agent resolution
 * @return 0 if there was no error or return the error code
 *
 * @see cyn_test_async, cyn_check_async
 */
extern
int
cyn_query_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key,
	int maxdepth
);

/**
 * Same as cyn_query_async but with a maxdepth of 0
 *
 * Note that the callback can be called during the call to that function.
 *
 * Note also that the callback will always be called either before the
 * function returns or else later.
 *
 * @param on_result_cb callback function receiving the result
 * @param closure closure for the callback
 * @param key key to be queried
 * @return
 *
 * @see cyn_query_async, cyn_check_async
 */
extern
int
cyn_test_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
);

/**
 * Same as cyn_query_async but with a default maxdepth for agent subqueries
 *
 * Note that the callback can be called during the call to that function.
 *
 * Note also that the callback will always be called either before the
 * function returns or else later.
 *
 * @param on_result_cb callback function receiving the result
 * @param closure closure for the callback
 * @param key key to be queried
 * @return 0 or -ENOMEM if some error occured
 *
 * @see cyn_query_async, cyn_test_async
 */
extern
int
cyn_check_async(
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
);

/**
 * Makes a recursive query asynchronous
 *
 * Note that the callback can be called during the call to that function.
 *
 * Note also that the callback will always be called either before the
 * function returns or else later.
 *
 * @param query the query
 * @param on_result_cb callback function receiving the result
 * @param closure closure for the callback
 * @param key key to be queried
 * @return
 */
extern
int
cyn_query_subquery_async(
	cynagora_query_t *query,
	on_result_cb_t *on_result_cb,
	void *closure,
	const data_key_t *key
);

/**
 * Send the reply to a query
 *
 * @param query the query to reply
 * @param value the reply value
 */
extern
void
cyn_query_reply(
	cynagora_query_t *query,
	const data_value_t *value
);

/**
 * Add the agent of name
 *
 * @param name name of the agent to add
 * @param agent_cb callback of the agent
 * @param closure closure of the callback of the agent
 * @return 0 in case of success
 *         -EINVAL if the name is too long
 *         -EEXIST if an agent of the same name is already recorded
 *         -ENOMEM if out of memory
 */
extern
int
cyn_agent_add(
	const char *name,
	agent_cb_t *agent_cb,
	void *closure
);

/**
 * Remove the agent of 'name'
 *
 * @param name name of the agent to remove
 * @return 0 in case of successful removal
 *         -EINVAL if the name is too long
 *         -ENOENT if the agent isn't recorded
 */
extern
int
cyn_agent_remove(
	const char *name
);

/**
 * Reset the changeid
 *
 * @see cyn_changeid, cyn_changeid_string
 */
extern
void
cyn_changeid_reset(
);

/**
 * Get the current changeid
 * @return the current changeid
 *
 * @see cyn_changeid_rest, cyn_changeid_string
 */
extern
uint32_t
cyn_changeid(
);

/**
 * Get the current changeid as a string
 * @return the string of the current change id
 *
 * @see cyn_changeid_rest, cyn_changeid
 */
extern
const char *
cyn_changeid_string(
);
