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
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#include "data.h"
#include "anydb.h"


#define CLIENT_MATCH_SCORE	1
#define SESSION_MATCH_SCORE	1
#define USER_MATCH_SCORE	1
#define PERMISSION_MATCH_SCORE	1

/******************************************************************************/
/******************************************************************************/
/*** UTILITIES                                                              ***/
/******************************************************************************/
/******************************************************************************/

/** check whether the 'text' fit String_Any, NULL or ""  */
static
bool
is_any(
	const char *text
) {
	return text == NULL || text[0] == 0 || (!text[1] && text[0] == Data_Any_Char);
}

/** check whether the 'text' fit String_Any, String_Wide, NULL or ""  */
static
bool
is_any_or_wide(
	const char *text
) {
	return text == NULL || text[0] == 0
		|| (!text[1] && (text[0] == Data_Any_Char || text[0] == Data_Wide_Char));
}

/** return the name of 'index' */
static
const char*
string(
	anydb_t *db,
	anydb_idx_t idx
) {
	if (idx == AnyIdx_Any)
		return Data_Any_String;
	if (idx == AnyIdx_Wide)
		return Data_Wide_String;
	return db->itf.string(db->clodb, idx);
}

/** search the index of 'name' and create it if 'create' */
static
int
idx(
	anydb_t *db,
	anydb_idx_t *idx,
	const char *name,
	bool create
) {
	/* special names */
	if (!name || !name[0]) {
		*idx = AnyIdx_Any;
		return 0;
	}
	if (!name[1]) {
		if (name[0] == Data_Any_Char) {
			*idx = AnyIdx_Any;
			return 0;
		}
		if (name[0] == Data_Wide_Char) {
			*idx = AnyIdx_Wide;
			return 0;
		}
	}

	/* other case */
	return db->itf.index(db->clodb, idx, name, create);
}

/** search the index of 'name' and create it if 'create' */
static
int
idx_but_any(
	anydb_t *db,
	anydb_idx_t *idx,
	const char *name,
	bool create
) {
	if (is_any_or_wide(name)) {
		*idx = AnyIdx_Wide;
		return 0;
	}

	/* other case */
	return db->itf.index(db->clodb, idx, name, create);
}

/** search the index of 'name' and create it if 'create' */
static
anydb_idx_t
idx_or_none_but_any(
	anydb_t *db,
	const char *name,
	bool create
) {
	anydb_idx_t idx;

	if (idx_but_any(db, &idx, name, create))
		idx = AnyIdx_None;
	return idx;
}

/******************************************************************************/
/******************************************************************************/
/*** FOR ALL                                                                ***/
/******************************************************************************/
/******************************************************************************/

struct for_all_s
{
	anydb_t *db;
	void *closure;
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value);
	anydb_idx_t idxcli;
	anydb_idx_t idxses;
	anydb_idx_t idxusr;
	const char *strperm;
	time_t now;
};

static
anydb_action_t
for_all_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	struct for_all_s *s = closure;
	data_key_t k;
	data_value_t v;

	if (value->expire && value->expire <= s->now)
		return Anydb_Action_Remove_And_Continue;

	if ((s->idxcli == AnyIdx_Any || s->idxcli == key->client)
	 && (s->idxses == AnyIdx_Any || s->idxses == key->session)
	 && (s->idxusr == AnyIdx_Any || s->idxusr == key->user)) {
		k.permission = string(s->db, key->permission);
		if (!s->strperm || !strcasecmp(s->strperm, k.permission)) {
			k.client = string(s->db, key->client);
			k.session = string(s->db, key->session);
			k.user = string(s->db, key->user);
			v.value = string(s->db, value->value);
			v.expire = value->expire;
			s->callback(s->closure, &k, &v);
		}
	}
	return Anydb_Action_Continue;
}

/** enumerate */
void
anydb_for_all(
	anydb_t *db,
	void *closure,
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value),
	const data_key_t *key
) {
	struct for_all_s s;

	s.db = db;
	s.closure = closure;
	s.callback = callback;

	if (idx(db, &s.idxcli, key->client, false)
	 || idx(db, &s.idxses, key->session, false)
	 || idx(db, &s.idxusr, key->user, false))
		return; /* nothing to do! because one of the idx doesn't exist */
	s.strperm = is_any(key->permission) ? NULL : key->permission;

	s.now = time(NULL);
	db->itf.apply(db->clodb, for_all_cb, &s);
}

/******************************************************************************/
/******************************************************************************/
/*** DROP                                                                   ***/
/******************************************************************************/
/******************************************************************************/

struct drop_s
{
	anydb_t *db;
	anydb_idx_t idxcli;
	anydb_idx_t idxses;
	anydb_idx_t idxusr;
	const char *strperm;
	time_t now;
};

static
anydb_action_t
drop_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	struct drop_s *s = closure;

	if (value->expire && value->expire <= s->now)
		return Anydb_Action_Remove_And_Continue;

	if ((s->idxcli == AnyIdx_Any || s->idxcli == key->client)
	 && (s->idxses == AnyIdx_Any || s->idxses == key->session)
	 && (s->idxusr == AnyIdx_Any || s->idxusr == key->user)
	 && (!s->strperm || !strcasecmp(s->strperm, string(s->db, key->permission))))
		return Anydb_Action_Remove_And_Continue;

	return Anydb_Action_Continue;
}

/** drop rules */
int
anydb_drop(
	anydb_t *db,
	const data_key_t *key
) {
	struct drop_s s;

	s.db = db;

	if (idx(db, &s.idxcli, key->client, false)
	 || idx(db, &s.idxses, key->session, false)
	 || idx(db, &s.idxusr, key->user, false))
		return 0; /* nothing to do! because one of the idx doesn't exist */
	s.strperm = is_any(key->permission) ? NULL : key->permission;

	s.now = time(NULL);
	db->itf.apply(db->clodb, drop_cb, &s);
	return 0;
}

/******************************************************************************/
/******************************************************************************/
/*** ADD                                                                    ***/
/******************************************************************************/
/******************************************************************************/

struct set_s
{
	anydb_t *db;
	anydb_idx_t idxcli;
	anydb_idx_t idxses;
	anydb_idx_t idxusr;
	anydb_idx_t idxval;
	time_t expire;
	const char *strperm;
	time_t now;
};

static
anydb_action_t
set_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	struct set_s *s = closure;

	if (value->expire && value->expire <= s->now)
		return Anydb_Action_Remove_And_Continue;

	if (s->idxcli == key->client
	 && s->idxses == key->session
	 && s->idxusr == key->user
	 && !strcasecmp(s->strperm, string(s->db, key->permission))) {
		value->value = s->idxval;
		value->expire = s->expire;
		s->db = NULL;
		return Anydb_Action_Update_And_Stop;
	}

	return Anydb_Action_Continue;
}

int
anydb_set(
	anydb_t *db,
	const data_key_t *key,
	const data_value_t *value
) {
	int rc;
	struct set_s s;
	anydb_key_t k;
	anydb_value_t v;

	s.db = db;
	s.strperm = key->permission;
	s.expire = value->expire;

	rc = idx_but_any(db, &s.idxcli, key->client, true);
	if (rc)
		goto error;
	rc = idx_but_any(db, &s.idxses, key->session, true);
	if (rc)
		goto error;
	rc = idx_but_any(db, &s.idxusr, key->user, true);
	if (rc)
		goto error;
	rc = idx(db, &s.idxval, value->value, true);
	if (rc)
		goto error;

	s.now = time(NULL);
	db->itf.apply(db->clodb, set_cb, &s);
	if (s.db) {
		if (idx(db, &k.permission, s.strperm, true))
			goto error;
		k.client = s.idxcli;
		k.user = s.idxusr;
		k.session = s.idxses;
		v.value = s.idxval;
		v.expire = s.expire;
		rc = db->itf.add(db->clodb, &k, &v);
		if (rc)
			goto error;
	}
	return 0;
error:
	return rc;
}

/******************************************************************************/
/******************************************************************************/
/*** TEST                                                                   ***/
/******************************************************************************/
/******************************************************************************/

struct test_s
{
	anydb_t *db;
	anydb_idx_t idxcli;
	anydb_idx_t idxses;
	anydb_idx_t idxusr;
	const char *strperm;
	int score;
	anydb_idx_t idxval;
	time_t expire;
	time_t now;
};

static
anydb_action_t
test_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	struct test_s *s = closure;
	int sc;

	if (value->expire && value->expire <= s->now)
		return Anydb_Action_Remove_And_Continue;

	if ((s->idxcli == key->client || key->client == AnyIdx_Wide)
	 && (s->idxses == key->session || key->session == AnyIdx_Wide)
	 && (s->idxusr == key->user || key->user == AnyIdx_Wide)
	 && (AnyIdx_Wide == key->permission
	     || !strcasecmp(s->strperm, string(s->db, key->permission)))) {
		sc = 1;
		if (key->client != AnyIdx_Wide)
			sc += CLIENT_MATCH_SCORE;
		if (key->session != AnyIdx_Wide)
			sc += SESSION_MATCH_SCORE;
		if (key->user != AnyIdx_Wide)
			sc += USER_MATCH_SCORE;
		if (key->permission != AnyIdx_Wide)
			sc += PERMISSION_MATCH_SCORE;
		if (sc > s->score) {
			s->score = sc;
			s->idxval = value->value;
			s->expire = value->expire;
		}
	}
	return Anydb_Action_Continue;
}

int
anydb_test(
	anydb_t *db,
	const data_key_t *key,
	data_value_t *value
) {
	struct test_s s;

	s.db = db;
	s.now = time(NULL);
	s.strperm = key->permission;
	s.expire = value->expire;

	s.idxcli = idx_or_none_but_any(db, key->client, true);
	s.idxses = idx_or_none_but_any(db, key->session, true);
	s.idxusr = idx_or_none_but_any(db, key->user, true);

	s.expire = -1;
	s.idxval = AnyIdx_Invalid;
	s.score = 0;

	db->itf.apply(db->clodb, test_cb, &s);

	if (s.score) {
		value->value = string(db, s.idxval);
		value->expire = s.expire;
	} else {
		value->value = NULL;
		value->expire = 0;
	}
	return s.score;
}

/******************************************************************************/
/******************************************************************************/
/*** CLEANUP                                                                ***/
/******************************************************************************/
/******************************************************************************/

static
anydb_action_t
cleanup_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	if (value->expire && value->expire <= *(time_t*)closure)
		return Anydb_Action_Remove_And_Continue;

	return Anydb_Action_Continue;
}

void
anydb_cleanup(
	anydb_t *db
) {
	time_t t;

	t = time(NULL);
	db->itf.apply(db->clodb, cleanup_cb, &t);
	db->itf.gc(db->clodb);
}

/******************************************************************************/
/******************************************************************************/
/*** DESTROY                                                                ***/
/******************************************************************************/
/******************************************************************************/

void
anydb_destroy(
	anydb_t *db
) {
	db->itf.destroy(db->clodb);
}
