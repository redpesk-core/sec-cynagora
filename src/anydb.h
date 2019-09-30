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
/* HIGH LEVEL ABSTRACTION OF THE DATABASES                                    */
/******************************************************************************/
/******************************************************************************/

/**
 * An index is an integer
 */
typedef uint32_t anydb_idx_t;

/*
 * Definition of some predefined indexes
 */

/** The invalid index */
#define AnyIdx_Invalid	((anydb_idx_t)0xffffffffu)

/** The index for ANY */
#define AnyIdx_Any	((anydb_idx_t)0xfffffffeu)

/** The index for WIDE */
#define AnyIdx_Wide	((anydb_idx_t)0xfffffffdu)

/** The index for NONE */
#define AnyIdx_None	((anydb_idx_t)0xfffffffcu)

/** The maximum value for indexes */
#define AnyIdx_Max	((anydb_idx_t)0xfffffff7u)

/**
 * A key is a set of index
 */
struct anydb_key {
	/** client string id */
	anydb_idx_t client;

	/** session string id */
	anydb_idx_t session;

	/** user string id */
	anydb_idx_t user;

	/** permission string id */
	anydb_idx_t permission;
};
typedef struct anydb_key anydb_key_t;

/**
 * A rule is a set of 32 bits integers
 */
struct anydb_value
{
	/** value string id */
	anydb_idx_t value;

	/**  expiration */
	time_t expire;
};
typedef struct anydb_value anydb_value_t;

/**
 * Operation of the transaction
 */
enum anydb_transaction
{
	/**
	 * Operation: Start a cancelable transaction
	 */
	Anydb_Transaction_Start = 0,

	/**
	 * Operation: Terminate the started transaction and commit its changes
	 */
	Anydb_Transaction_Commit = 1,

	/**
	 * Operation: Terminate the started transaction and cancel its changes
	 */
	Anydb_Transaction_Cancel = 2
};
typedef enum anydb_transaction anydb_transaction_t;

/**
 * Actions to perform in response to anydb_applycb_t callbacks.
 */
enum anydb_action
{
	/** Continue to apply with the next element of the database */
	Anydb_Action_Continue = 0,

	/** Stop to apply */
	Anydb_Action_Stop = 1,

	/** Update the current element (implicitly, also continue) */
	Anydb_Action_Update = 2,

	/** Remove the current element (implicitly, also continue) */
	Anydb_Action_Remove = 4,

	/** Update the current element and stop to apply */
	Anydb_Action_Update_And_Stop = Anydb_Action_Update | Anydb_Action_Stop,

	/** Update the current element and continue to apply */
	Anydb_Action_Update_And_Continue = Anydb_Action_Update | Anydb_Action_Continue,

	/** Remove the current element and stop to apply */
	Anydb_Action_Remove_And_Stop = Anydb_Action_Remove | Anydb_Action_Stop,

	/** Remove the current element and continue to apply */
	Anydb_Action_Remove_And_Continue = Anydb_Action_Remove | Anydb_Action_Continue
};
typedef enum anydb_action anydb_action_t;

/**
 * Callback of apply method. This callback is called for any item of
 * the database and it tells through its return what the anydb has
 * to do: see anydb_action_t.
 * The 'closure' is the closure given by the caller of 'apply' method.
 * 'key' is the iterated key of the anydb. It can not be changed.
 * 'value' is the value stored in the database for the key.
 */
typedef anydb_action_t anydb_applycb_t(void *closure, const anydb_key_t *key, anydb_value_t *value);

/**
 * Interface to any database implementation
 */
struct anydb_itf
{
	/**
	 * Get the index of the 'name' in 'idx'. If the name is found
	 * then its index is returned. If the name is not found, the
	 * database backend has to create it if 'create' is not zero.
	 * 'clodb' is the database's closure.
	 * Returns 0 in case of success (*idx filled with the index)
	 * or return a negative error code in -errno like form.
	 */
	int (*index)(void *clodb, anydb_idx_t *idx, const char *name, bool create);

	/**
	 * Get the string for the index 'idx'. idx MUST be valid.
	 * 'clodb' is the database's closure.
	 */
	const char *(*string)(void *clodb, anydb_idx_t idx);

	/**
	 * Start, Commit or Cancel a cancellable transaction. The operation
	 * to perform is given by 'op'.
	 * 'clodb' is the database's closure.
	 * Returns 0 in case of success or return a negative error code
	 * in -errno like form.
	 */
	int (*transaction)(void *clodb, anydb_transaction_t atomic_op);

	/**
	 * Iterate over the database items and apply the operator 'oper'.
	 * The callback operator 'oper' is called with the given 'closure'
	 * and the key and value for the item. It can modify or delete the item.
	 * 'clodb' is the database's closure.
	 */
	void (*apply)(void *clodb, anydb_applycb_t *oper, void *closure);

	/**
	 * Add the item of 'key' and 'value'.
	 * 'clodb' is the database's closure.
	 * Returns 0 in case of success or return a negative error code
	 * in -errno like form.
	 */
	int (*add)(void *clodb, const anydb_key_t *key, const anydb_value_t *value);

	/**
	 * Garbage collection of unused items.
	 * 'clodb' is the database's closure.
	 */
	void (*gc)(void *clodb);

	/**
	 * Synchronize the database and its longterm support (file)
	 * 'clodb' is the database's closure.
	 * Returns 0 in case of success or return a negative error code
	 * in -errno like form.
	 */
	int (*sync)(void *clodb);

	/**
	 * Destroys the database
	 * 'clodb' is the database's closure.
	 */
	void (*destroy)(void *clodb);
};
typedef struct anydb_itf anydb_itf_t;

/**
 * The structure for abstracting backend databases
 */
struct anydb
{
	/** the closure */
	void *clodb;

	/** the implementation methods */
	anydb_itf_t itf;
};
typedef struct anydb anydb_t;

/**
 * Manage atomicity of modifications by enabling cancellation
 * @param db database to manage
 * @param oper operation to perform
 * @return 0 in case of success or a negative error code in -errno like form.
 */
extern
int
anydb_transaction(
	anydb_t *db,
	anydb_transaction_t oper
);

/**
 * Enumerate items of the database matching the given key
 * @param db database to enumerate
 * @param callback callback function receiving the item that matches the key
 * @param closure closure for the callback
 * @param key key to restrict enumeration can't be NULL
 */
extern
void
anydb_for_all(
	anydb_t *db,
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value),
	void *closure,
	const data_key_t *key
);

/**
 * Drop any rule that matches the key
 * @param db database to modify
 * @param key the key that select items to be dropped
 */
extern
void
anydb_drop(
	anydb_t *db,
	const data_key_t *key
);

/**
 * Set the rule described by key and value
 * @param db the database to set
 * @param key the key of the rule
 * @param value the value of the rule
 * @return 0 on success or a negative error code
 */
extern
int
anydb_set(
	anydb_t *db,
	const data_key_t *key,
	const data_value_t *value
);

/**
 * Test a rule and return its score and the value
 * @param db the database
 * @param key key to be matched by rules
 * @param value value found for the key, filled only if a key matched
 * @return 0 if no rule matched or a positive integer when the rule matched
 * The higher the integer is, the more accurate is the rule found.
 */
extern
unsigned
anydb_test(
	anydb_t *db,
	const data_key_t *key,
	data_value_t *value
);

/**
 * Drop any expired rule
 * @param db the database to clean
 */
extern
void
anydb_cleanup(
	anydb_t *db
);

/**
 * Is the database empty?
 * @param db the database to test
 * @return true if the database is empty or otherwise false
 */
extern
bool
anydb_is_empty(
	anydb_t *db
);

/**
 * Synchronize the database if needed
 * @param db the database to
 * @return 0 on success or a negative -errno like code
 */
extern
int
anydb_sync(
	anydb_t *db
);

/**
 * Destroy the database
 * @param db the database to destroy
 */
extern
void
anydb_destroy(
	anydb_t *db
);
