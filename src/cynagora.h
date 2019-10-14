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

/******************************************************************************/
/* COMMON PART - types and functions common to check/admin/agent clients      */
/******************************************************************************/
typedef struct cynagora cynagora_t;
typedef enum cynagora_type cynagora_type_t;
typedef struct cynagora_key cynagora_key_t;

/**
 * type of the client interface
 */
enum cynagora_type
{
	/** type for checking permissions */
	cynagora_Check,

	/** type for adminstration */
	cynagora_Admin,

	/** type for handling agents */
	cynagora_Agent
};

/**
 * Describes a query key
 */
struct cynagora_key
{
	/** client item of the key */
	const char *client;
	/** session item of the key */
	const char *session;
	/** user item of the key */
	const char *user;
	/** permission item of the key */
	const char *permission;
};

/**
 * Callback for receiving asynchronously the replies to the queries
 * Receives:
 *  closure: the closure given to cynagora_async_check
 *  status: 0 if forbidden
 *          1 if granted
 *          -ECANCELED if canceled
 */
typedef void cynagora_async_check_cb_t(
		void *closure,
		int status);

/**
 * Callback for managing the connection in an external loop
 *
 * That callback receives epoll_ctl operations, a file descriptor number and
 * a mask of expected events.
 *
 * @see epoll_ctl
 */
typedef int cynagora_async_ctl_cb_t(
			void *closure,
			int op,
			int fd,
			uint32_t events);

/**
 * Create a client to the permission server cynagora
 * The client is created but not connected. The connection is made on need.
 *
 * @param cynagora   pointer to the handle of the opened client
 * @param type       type of the client to open
 * @param cache_size requested cache size (no cache if 0)
 * @param socketspec specification of the socket to connect to or NULL for
 *                   using the default
 *
 * @return 0 in case of success and in that case *cynagora is filled
 *         a negative -errno value and *cynara is set to NULL
 *
 * @see cynagora_destroy, cynagora_cache_resize
 */
extern
int
cynagora_create(
	cynagora_t **cynagora,
	cynagora_type_t type,
	uint32_t cache_size,
	const char *socketspec
);

/**
 * Destroy the client handler and release its memory
 *
 * @param cynagora the client handler to close
 *
 * @see cynagora_create
 */
extern
void
cynagora_destroy(
	cynagora_t *cynagora
);

/**
 * Ask the client to disconnect from the server.
 * The client will reconnect if needed.
 *
 * @param cynagora the client handler
 */
extern
void
cynagora_disconnect(
	cynagora_t *cynagora
);

/**
 * Set the asynchronous control function
 *
 * @param cynagora  the handler of the client
 * @param controlcb
 * @param closure
 *
 * @return  0 in case of success or a negative -errno value
 */
extern
int
cynagora_async_setup(
	cynagora_t *cynagora,
	cynagora_async_ctl_cb_t *controlcb,
	void *closure
);

/**
 * Process the inputs of the client
 *
 * @param cynagora  the handler of the client
 *
 * @return  0 in case of success or a negative -errno value
 */
extern
int
cynagora_async_process(
	cynagora_t *cynagora
);

/**
 * Clear the cache
 *
 * @param cynagora the client handler
 *
 * @see cynagora_cache_resize
 */
extern
void
cynagora_cache_clear(
	cynagora_t *cynagora
);

/**
 * Resize the cache
 *
 * @param cynagora the client handler
 * @param size     new expected cache
 *
 * @return 0 on success or -ENOMEM if out of memory
 *
 * @see cynagora_cache_clear, cynagora_create
 */
extern
int
cynagora_cache_resize(
	cynagora_t *cynagora,
	uint32_t size
);

/**
 * Check a key against the cache
 *
 * @param cynagora the client handler
 * @param key the key to check
 *
 * @return 0 if forbidden, 1 if authorize, -ENOENT if cache miss
 *
 * @see cynagora_check
 */
extern
int
cynagora_cache_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key
);

/**
 * Query the permission database for the key (synchronous)
 * Allows agent resolution.
 *
 * @param cynagora the client handler
 * @param key      the key to check
 *
 * @return 0 if permission forbidden, 1 if permission granted
 *         or if error a negative -errno value
 *
 * @see cynagora_test, cynagora_cache_check
 */
extern
int
cynagora_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key
);

/**
 * Query the permission database for the key (synchronous)
 * Avoids agent resolution.
 *
 * @param cynagora the client handler
 * @param key
 * @return
 *
 * @see cynagora_check
 */
extern
int
cynagora_test(
	cynagora_t *cynagora,
	const cynagora_key_t *key
);

/**
 * Check the key asynchronousely (async)
 *
 * @param cynagora  the handler of the client
 * @param key       the key to query
 * @param simple    if zero allows agent process else if not 0 forbids it
 * @param callback  the callback to call on reply
 * @param closure   a closure for the callback
 *
 * @return  0 in case of success or a negative -errno value
 */
extern
int
cynagora_async_check(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	int simple,
	cynagora_async_check_cb_t *callback,
	void *closure
);

/******************************************************************************/
/* ADMIN PART - types and functions specific to admin clients                 */
/******************************************************************************/

/**
 * Describes the value associated to a key
 */
struct cynagora_value
{
	/** the associated value */
	const char *value;
	/** the expiration in seconds since epoch, negative to avoid cache */
	time_t expire;
};
typedef struct cynagora_value cynagora_value_t;

/**
 * Callback for enumeration of items (admin)
 * The function is called for each entry matching the selection key
 * with the key and the associated value for that entry
 *
 * @see cynagora_get
 */
typedef void cynagora_get_cb_t(
			void *closure,
			const cynagora_key_t *key,
			const cynagora_value_t *value);


/**
 * List any value of the permission database that matches the key (admin, synchronous)
 *
 * @param cynagora the client handler
 * @param key      the selection key
 * @param callback the callback for receiving items
 * @param closure  closure of the callback
 *
 * @return 0 in case of success or a negative -errno value
 */
extern
int
cynagora_get(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	cynagora_get_cb_t *callback,
	void *closure
);

/**
 * Query or set the logging of requests (admin, synchronous)
 *
 * @param cynagora the client handler
 * @param on       should set on
 * @param off      should set off
 *
 * @return 0 if not logging, 1 if logging or a negative -errno value
 */
extern
int
cynagora_log(
	cynagora_t *cynagora,
	int on,
	int off
);

/**
 * Enter cancelable section for modifying database (admin, synchronous)
 *
 * @param cynagora the handler of the client
 *
 * @return 0 in case of success or a negative -errno value
 *         -EPERM if not a admin client
 *         -EINPROGRESS if already entered
 *
 * @see cynagora_leave, cynagora_set, cynagora_drop
 */
extern
int
cynagora_enter(
	cynagora_t *cynagora
);

/**
 * Leave cancelable section for modifying database (admin, synchronous)
 *
 * @param cynagora  the handler of the client
 * @param commit    if zero, cancel the modifications in progress otherwise if
 *                  not zero, commit the changes
 *
 * @return 0 in case of success or a negative -errno value
 *         -EPERM if not a admin client
 *         -ECANCELED if not entered
 *
 * @see cynagora_enter, cynagora_set, cynagora_drop
 */
extern
int
cynagora_leave(
	cynagora_t *cynagora,
	int commit
);

/**
 * Set a rule (either create or change it) (admin, synchronous)
 * This call requires to have entered the cancelable section.
 *
 * @param cynagora  the handler of the client
 * @param key       the key to set
 * @param value     the value to set to the key
 *
 * @return 0 in case of success or a negative -errno value
 *         -EPERM if not a admin client
 *         -ECANCELED if not entered
 *
 * @see cynagora_enter, cynagora_leave, cynagora_drop
 */
extern
int
cynagora_set(
	cynagora_t *cynagora,
	const cynagora_key_t *key,
	const cynagora_value_t *value
);

/**
 * Drop items matching the key selector (admin, synchronous)
 * This call requires to have entered the cancelable section.
 *
 * @param cynagora  the handler of the client
 * @param key       Filter of the keys to drop
 *
 * @return  0 in case of success or a negative -errno value
 *         -EPERM if not a admin client
 *         -ECANCELED if not entered
 *
 * @see cynagora_enter, cynagora_leave, cynagora_set
 */
extern
int
cynagora_drop(
	cynagora_t *cynagora,
	const cynagora_key_t *key
);


/******************************************************************************/
/* AGENT PART - types and functions specific to agent clients                 */
/******************************************************************************/

/** structure representing an agent query from cynagora server */
struct cynagora_query
{
	/** name of the queried agent */
	const char *name;

	/** value associated to the matching rule */
	const char *value;

	/** key of the query */
	cynagora_key_t key;
};
typedef struct cynagora_query cynagora_query_t;

/** callback receiving agent queries */
typedef int (cynagora_agent_cb_t)(
		void *closure,
		cynagora_query_t *query);

/**
 * Check if the given name is a valid agent name
 *
 * @param name name to check
 * @return 0 when invalid or 1 if valid
 */
extern
int
cynagora_agent_is_valid_name(
	const char *name
);

/**
 * Create an agent of a given name
 *
 * @param cynagora the client
 * @param name name of the agent to create
 * @param agentcb callback that will treat queries for the agent
 * @param closure closure for the callback
 * @return 0 on success
 */
extern
int
cynagora_agent_create(
	cynagora_t *cynagora,
	const char *name,
	cynagora_agent_cb_t *agentcb,
	void *closure
);

/**
 * Reply to the query. After calling that function, the query is not more
 * valid (removed from memory).
 *
 * @param query the query to reply
 * @param value the value that the agent wants to reply to the query
 * @return 0 on success or a negative error code
 */
extern
int
cynagora_agent_reply(
	cynagora_query_t *query,
	cynagora_value_t *value
);
