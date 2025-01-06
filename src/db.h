/*
 * Copyright (C) 2018-2025 IoT.bzh Company
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
/* INTERNAL DATABASE IMPLEMENTATION                                           */
/******************************************************************************/
/******************************************************************************/

/**
 * Open the database in the directory
 *
 * @param directory the directory containing the database
 * @return 0 in case of success or a negative error code
 *
 * @see db_close
 */
extern
int
db_open(
	const char *directory
);

/**
 * close the database
 */
extern
void
db_close(
);

/**
 * Is the database empty?
 *
 * @return true if empty or else false
 */
extern
bool
db_is_empty(
);

/**
 * Enter atomic mode or cancelable mode
 *
 * @return 0 in case of success or a negative -errno like value
 *
 * @see db_transaction_end, db_drop, db_set
 */
extern
int
db_transaction_begin(
);

/**
 * Leave atomic mode commiting or not the changes
 *
 * @param commit if true changes are commited otherwise, if false cancel them
 * @return 0 in case of success or a negative -errno like value
 *
 * @see db_transaction_begin, db_drop, db_set
 */
extern
int
db_transaction_end(
	bool commit
);

/**
 * Erase rules matching the key
 *
 * @param key the search key for the rules to remove
 * @return 0 in case of success or a negative -errno like value
 *
 * @see db_transaction_begin, db_transaction_end, db_set
 */
extern
int
db_drop(
	const data_key_t *key
);

/**
 * Add the rule of key and value
 *
 * @param key the key of the rule to add
 * @param value the value of the rule
 * @return 0 in case of success or a negative -errno like value
 *
 * @see db_transaction_begin, db_transaction_end, db_drop
 */
extern
int
db_set(
	const data_key_t *key,
	const data_value_t *value
);

/**
 * Iterate over rules matching the key: call the callback for each found item
 *
 * @param callback the callback function to be call for each rule matching key
 * @param closure the closure of the callback
 * @param key the searching key
 */
extern
void
db_for_all(
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value),
	void *closure,
	const data_key_t *key
);

/**
 * Get the rule value for the key
 *
 * @param key The key to query
 * @param value Where to store the result if any
 * @return 0 if no rule matched (value unchanged then) or a positive integer
 *  when a value was found for the key
 */
extern
unsigned
db_test(
	const data_key_t *key,
	data_value_t *value
);

/**
 * Cleanup the database by removing expired items
 *
 * @return 0 in case of success or a negative -errno like value
 */
extern
int
db_cleanup(
);

/**
 * Write the database to the file system (synchrnize it)
 *
 * @return 0 in case of success or a negative -errno like value
 */
extern
int
db_sync(
);
