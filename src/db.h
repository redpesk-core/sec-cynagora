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

#define MAX_NAME_LENGTH 32767

/** open the database for files 'names' and 'rules' (can be NULL) */
extern
int
db_open(
	const char *directory
);

/** close the database */
extern
void
db_close(
);

/** is the database empty */
extern
bool
db_is_empty(
);

/** sync the database */
extern
int
db_sync(
);

/** get an index for a name */
extern
int
db_get_name_index(
	uint32_t *index,
	const char *name,
	bool needed
);

/** make a backup of the database */
extern
int
db_backup(
);

/** recover the database from latest backup */
extern
int
db_recover(
);

/** enumerate */
extern
void
db_for_all(
	void *closure,
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value),
	const data_key_t *key
);

/** erase rules */
extern
int
db_drop(
	const data_key_t *key
);

/** set rules */
extern
int
db_set(
	const data_key_t *key,
	const data_value_t *value
);

/** check rules */
extern
int
db_test(
	const data_key_t *key,
	data_value_t *value
);

/** cleanup the base */
extern
int
db_cleanup(
);

