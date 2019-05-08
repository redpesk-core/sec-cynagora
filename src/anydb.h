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

/**
 * An index is an integer
 */
typedef uint32_t anydb_idx_t;

/*
 * Definition of some predefined indexes
 */

/** The invalid index */
#define AnyIdx_Invalid	((anydb_idx_t)0xffffffffu)

/**  */
#define AnyIdx_Any	((anydb_idx_t)0xfffffffeu)
#define AnyIdx_Wide	((anydb_idx_t)0xfffffffdu)
#define AnyIdx_None	((anydb_idx_t)0xfffffffcu)
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
 */
enum anydb_action
{
	Anydb_Action_Continue = 0,
	Anydb_Action_Update_And_Stop = 1,
	Anydb_Action_Remove_And_Continue = 2
};
typedef enum anydb_action anydb_action_t;

enum anydb_transaction
{
	Anydb_Transaction_Start = 0,
	Anydb_Transaction_Commit = 1,
	Anydb_Transaction_Cancel = 2
};
typedef enum anydb_transaction anydb_transaction_t;

struct anydb_itf
{
	int (*index)(void *clodb, anydb_idx_t *idx, const char *name, bool create);
	const char *(*string)(void *clodb, anydb_idx_t idx);
	int (*transaction)(void *clodb, anydb_transaction_t atomic_op);
	void (*apply)(void *clodb, anydb_action_t (*oper)(void *closure, const anydb_key_t *key, anydb_value_t *value), void *closure);
	int (*add)(void *clodb, const anydb_key_t *key, const anydb_value_t *value);
	void (*gc)(void *clodb);
	void (*destroy)(void *clodb);
};
typedef struct anydb_itf anydb_itf_t;

struct anydb
{
	void *clodb;
	anydb_itf_t itf;
};
typedef struct anydb anydb_t;

/** manage atomicity of operations */
extern
int
anydb_transaction(
	anydb_t *db,
	anydb_transaction_t oper
);

/** enumerate */
extern
void
anydb_for_all(
	anydb_t *db,
	void *closure,
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value),
	const data_key_t *key
);

/** drop rules */
extern
int
anydb_drop(
	anydb_t *db,
	const data_key_t *key
);

/** set a rules */
extern
int
anydb_set(
	anydb_t *db,
	const data_key_t *key,
	const data_value_t *value
);

/** test a rule, returns 0 or the score: count of exact keys */
extern
int
anydb_test(
	anydb_t *db,
	const data_key_t *key,
	data_value_t *value
);

/** drop rules */
extern
void
anydb_cleanup(
	anydb_t *db
);

/** destroy the database */
extern
void
anydb_destroy(
	anydb_t *db
);
