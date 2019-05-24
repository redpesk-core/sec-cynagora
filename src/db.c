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

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include "data.h"
#include "anydb.h"
#include "filedb.h"
#include "memdb.h"
#include "db.h"

static anydb_t *memdb;
static anydb_t *filedb;

/** check whether the 'text' fit String_Any, String_Wide, NULL or ""  */
static
bool
is_any_or_wide(
	const char *text
) {
	return text == NULL || text[0] == 0
		|| (!text[1] && (text[0] == Data_Any_Char || text[0] == Data_Wide_Char));
}


/** open the database for files 'names' and 'rules' (can be NULL) */
int
db_open(
	const char *directory
) {
	int rc;

	rc = memdb_create(&memdb);
	if (!rc) {
		rc = filedb_create(&filedb, directory, NULL);
		if (rc)
			anydb_destroy(memdb);
	}
	return rc;
}

/** close the database */
void
db_close(
) {
	anydb_destroy(filedb);
	anydb_destroy(memdb);
}

/** is the database empty */
bool
db_is_empty(
) {
	return anydb_is_empty(filedb);
}

/** enter atomic mode */
int
db_transaction_begin(
) {
	int rc1, rc2;

	rc1 = anydb_transaction(filedb, Anydb_Transaction_Start);
	rc2 = anydb_transaction(memdb, Anydb_Transaction_Start);

	return rc1 ?: rc2;
}

/** leave atomic mode */
int
db_transaction_end(
	bool commit
) {
	int rc1, rc2, rc3, rc4;

	if (commit) {
		rc1 = anydb_transaction(filedb, Anydb_Transaction_Commit);
		rc2 = anydb_transaction(memdb, Anydb_Transaction_Commit);
		rc3 = db_cleanup();
	} else {
		rc1 = anydb_transaction(filedb, Anydb_Transaction_Cancel);
		rc2 = anydb_transaction(memdb, Anydb_Transaction_Cancel);
		rc3 = 0;
	}
	rc4 = db_sync();

	return rc1 ?: rc2 ?: rc3 ?: rc4;
}


/** enumerate */
void
db_for_all(
	void *closure,
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value),
	const data_key_t *key
) {
	anydb_for_all(filedb, closure, callback, key);
	anydb_for_all(memdb, closure, callback, key);
}

/** drop rules */
int
db_drop(
	const data_key_t *key
) {
	anydb_drop(filedb, key);
	anydb_drop(memdb, key);
	return 0;
}

/** set rules */
int
db_set(
	const data_key_t *key,
	const data_value_t *value
) {
	if (is_any_or_wide(key->session))
		return anydb_set(filedb, key, value);
	else
		return anydb_set(memdb, key, value);
}

/** check rules */
int
db_test(
	const data_key_t *key,
	data_value_t *value
) {
	int s1, s2;
	data_value_t v1, v2;

	s1 = anydb_test(memdb, key, &v1);
	s2 = anydb_test(filedb, key, &v2);
	if (s2 > s1) {
		*value = v2;
		return s2;
	} else {
		*value = v1;
		return s1;
	}
}

int
db_cleanup(
) {
	anydb_cleanup(filedb);
	anydb_cleanup(memdb);
	return 0;
}

int
db_sync(
) {
	int rc1 = anydb_sync(filedb);
	int rc2 = anydb_sync(memdb);
	return rc1 ?: rc2;
}

