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
/******************************************************************************/
/******************************************************************************/
/* INTERNAL DATABASE IMPLEMENTATION                                           */
/******************************************************************************/
/******************************************************************************/

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
static bool modifiable;

/**
 * check whether the 'text' fit String_Any, String_Wide, NULL or ""
 * @param text the text to check
 * @return true if ANY or WIDE
 */
static
bool
is_any_or_wide(
	const char *text
) {
	return text == NULL || text[0] == 0
		|| (!text[1] && (text[0] == Data_Any_Char || text[0] == Data_Wide_Char));
}


/* see db.h */
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

/* see db.h */
void
db_close(
) {
	anydb_destroy(filedb);
	anydb_destroy(memdb);
}

/* see db.h */
bool
db_is_empty(
) {
	return anydb_is_empty(filedb);
}

/* see db.h */
int
db_transaction_begin(
) {
	int rc1, rc2, rc;

	if (modifiable)
		return -EALREADY;

	rc1 = anydb_transaction(filedb, Anydb_Transaction_Start);
	rc2 = anydb_transaction(memdb, Anydb_Transaction_Start);

	rc = rc1 ?: rc2;
	modifiable = !rc;

	return rc;
}

/* see db.h */
int
db_transaction_end(
	bool commit
) {
	int rc1, rc2, rc3, rc4;

	if (!modifiable)
		return -EALREADY;

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
	modifiable = false;

	return rc1 ?: rc2 ?: rc3 ?: rc4;
}


/* see db.h */
void
db_for_all(
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value),
	void *closure,
	const data_key_t *key
) {
	anydb_for_all(filedb, callback, closure, key);
	anydb_for_all(memdb, callback, closure, key);
}

/* see db.h */
int
db_drop(
	const data_key_t *key
) {
	if (!modifiable)
		return -EACCES;

	anydb_drop(filedb, key);
	anydb_drop(memdb, key);
	return 0;
}

/* see db.h */
int
db_set(
	const data_key_t *key,
	const data_value_t *value
) {
	anydb_t *db;

	if (!modifiable)
		return -EACCES;

	/* if session is any or wide, it is permanent rule in file, otherwise in memory */
	db = is_any_or_wide(key->session) ? filedb : memdb;
	return anydb_set(db, key, value);
}

/* see db.h */
unsigned
db_test(
	const data_key_t *key,
	data_value_t *value
) {
	unsigned s1, s2;
	data_value_t v1, v2;

	s1 = anydb_test(memdb, key, &v1);
	s2 = anydb_test(filedb, key, &v2);
	if (s2 > s1) {
		*value = v2;
		return s2;
	}
	if (s1)
		*value = v1;
	return s1;
}

/* see db.h */
int
db_cleanup(
) {
	anydb_cleanup(filedb);
	anydb_cleanup(memdb);
	return 0;
}

/* see db.h */
int
db_sync(
) {
	int rc1 = anydb_sync(filedb);
	int rc2 = anydb_sync(memdb);
	return rc1 ?: rc2;
}

