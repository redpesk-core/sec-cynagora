/*
 * Copyright (C) 2018-2023 IoT.bzh Company
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
/* HIGH LEVEL ABSTRACTION OF THE DATABASES                                    */
/******************************************************************************/
/******************************************************************************/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#include "data.h"
#include "anydb.h"

/*
 * Definition of the score for matching keys against database when querying.
 * The scores defined below are used in the function 'searchkey_test'.
 *
 * They are used to give a score the key searched in the objective to
 * select the rule to apply: the selected rule is the rule of higher score.
 *
 * When a rule matches, all its keys matched but some of them are wild matches.
 * The key's scores of the keys that are not wild selectors are added.
 *
 * A key's score is made of 2 parts:
 *   - the lower 4 bits are used as bit fields to priority individual keys
 *   - the upper hight bits are used to count the number of keys that matched
 *
 * So the rule to apply is given by:
 *
 *  1. The rules that matches exactly more keys
 *  2. If 1 apply to more than one rule, the select rule is the one
 *     matches more exactly the keys in the following order of priority:
 *      - session
 *      - user
 *      - client
 *      - permission
 */
#define KEY_SESSION_MATCH_SCORE		0x18
#define KEY_USER_MATCH_SCORE		0x14
#define KEY_CLIENT_MATCH_SCORE		0x12
#define KEY_PERMISSION_MATCH_SCORE	0x11
#define SOME_MATCH_SCORE		0x10
#define NO_MATCH_SCORE			0x00

/**
 * helper for searching items
 */
union searchkey {
	struct {
		/** client id */
		anydb_idx_t idxcli;

		/** session id */
		anydb_idx_t idxses;

		/** user id */
		anydb_idx_t idxusr;

		/** permission string */
		const char *strperm;
	};
	anydb_key_t key;
};
typedef union searchkey searchkey_t;

/******************************************************************************/
/******************************************************************************/
/*** UTILITIES                                                              ***/
/******************************************************************************/
/******************************************************************************/

/**
 * Check whether the 'text' fit String_Any, NULL or ""
 * @param text the text to check
 * @return true if text matches ANY possible values
 */
static
bool
is_any(
	const char *text
) {
	return text == NULL || text[0] == 0 || (!text[1] && text[0] == Data_Any_Char);
}

/**
 * check whether the 'text' fit String_Any, String_Wide, NULL or ""
 * @param text the text to check
 * @return true if text matches ANY or WIDE possible values
 */
static
bool
is_any_or_wide(
	const char *text
) {
	return text == NULL || text[0] == 0
		|| (!text[1] && (text[0] == Data_Any_Char || text[0] == Data_Wide_Char));
}

/**
 * Get the name stored for index
 * @param db the anydb database to query
 * @param idx the index of the string to get
 * @return the name or NULL if doesn't exist
 */
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

/**
 * Search the index of 'name' and create it if 'create'
 * @param db the anydb database to query
 * @param idx where to store the result if needed
 * @param name name to search and/or create
 * @param create if not nul, the name is created if it doesn't exist
 * @return 0 in case of success of -errno
 */
static
int
idx(
	anydb_t *db,
	anydb_idx_t *idx,
	const char *name,
	bool create
) {
	/* handle special names */
	if (!name || !name[0]) {
		/* no name or empty name means ANY */
		*idx = AnyIdx_Any;
		return 0;
	}
	/* handle special names of one character  */
	if (!name[1]) {
		if (name[0] == Data_Any_Char) {
			/* Single char for ANY */
			*idx = AnyIdx_Any;
			return 0;
		}
		if (name[0] == Data_Wide_Char) {
			/* Single char for WIDE */
			*idx = AnyIdx_Wide;
			return 0;
		}
	}

	/* other case: ask the database backend */
	return db->itf.index(db->clodb, idx, name, create);
}

/**
 * Search the index of 'name' and create it if 'create'
 * Return the index for WIDE if name matches ANY or WIDE
 * @param db the backend database
 * @param idx where to store the found index
 * @param name the name to search or create
 * @param create not nul for authorizing creation of the index for name
 * @return 0 on success
 */
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

/**
 * Return the index of 'name' in the database 'db'. In option 'create' it.
 * If the name encode ANY or WIDE returns WIDE.
 * @param db the backend database
 * @param name the name to search or create
 * @param create not nul for authorizing creation of the index for name
 * @return the found index or AnyIdx_None if not found.
 */
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
/*** EXPIRATION                                                             ***/
/******************************************************************************/
/******************************************************************************/

static
bool
expired(
	time_t expire,
	time_t now
) {
	if (expire < 0)
		expire = -(expire + 1);
	return expire && expire <= now;
}

/******************************************************************************/
/******************************************************************************/
/*** SEARCH KEYS                                                            ***/
/******************************************************************************/
/******************************************************************************/

static
bool
searchkey_prepare_match(anydb_t *db,
	const data_key_t *key,
	searchkey_t *skey,
	bool create
) {
	if (idx(db, &skey->idxcli, key->client, create)
	 || idx(db, &skey->idxses, key->session, create)
	 || idx(db, &skey->idxusr, key->user, create))
		return false; /* one of the idx doesn't exist */
	skey->strperm = is_any(key->permission) ? NULL : key->permission;

	return true;
}

static
bool
searchkey_match(
	anydb_t *db,
	const anydb_key_t *key,
	const searchkey_t *skey
) {
	return (skey->idxcli == AnyIdx_Any || skey->idxcli == key->client)
	    && (skey->idxses == AnyIdx_Any || skey->idxses == key->session)
	    && (skey->idxusr == AnyIdx_Any || skey->idxusr == key->user)
	    && (!skey->strperm || !strcasecmp(skey->strperm, string(db, key->permission)));
}

static
int
searchkey_prepare_is(
	anydb_t *db,
	const data_key_t *key,
	searchkey_t *skey,
	bool create
) {
	int rc;

	rc = idx_but_any(db, &skey->idxcli, key->client, create);
	if (!rc) {
		rc = idx_but_any(db, &skey->idxses, key->session, create);
		if (!rc) {
			rc = idx_but_any(db, &skey->idxusr, key->user, create);
			if (!rc)
				skey->strperm = key->permission;
		}
	}
	return rc;
}

static
bool
searchkey_is(
	anydb_t *db,
	const anydb_key_t *key,
	const searchkey_t *skey
) {
	return skey->idxcli == key->client
	    && skey->idxses == key->session
	    && skey->idxusr == key->user
	    && !strcasecmp(skey->strperm, string(db, key->permission));
}

static
void
searchkey_prepare_test(
	anydb_t *db,
	const data_key_t *key,
	searchkey_t *skey,
	bool create
) {
	skey->idxcli = idx_or_none_but_any(db, key->client, create);
	skey->idxses = idx_or_none_but_any(db, key->session, create);
	skey->idxusr = idx_or_none_but_any(db, key->user, create);
	skey->strperm = key->permission;
}

static
unsigned
searchkey_test(
	anydb_t *db,
	const anydb_key_t *key,
	const searchkey_t *skey
) {
	unsigned sc;

	if ((key->client  != AnyIdx_Wide && skey->idxcli != key->client)
	 || (key->session != AnyIdx_Wide && skey->idxses != key->session)
	 || (key->user    != AnyIdx_Wide && skey->idxusr != key->user)
	 || (key->permission != AnyIdx_Wide
	        && strcasecmp(skey->strperm, string(db, key->permission)))) {
		sc = NO_MATCH_SCORE;
	} else {
		sc = SOME_MATCH_SCORE;
		if (key->client != AnyIdx_Wide)
			sc += KEY_CLIENT_MATCH_SCORE;
		if (key->session != AnyIdx_Wide)
			sc += KEY_SESSION_MATCH_SCORE;
		if (key->user != AnyIdx_Wide)
			sc += KEY_USER_MATCH_SCORE;
		if (key->permission != AnyIdx_Wide)
			sc += KEY_PERMISSION_MATCH_SCORE;
	}
	return sc;
}

/******************************************************************************/
/******************************************************************************/
/*** FOR ALL                                                                ***/
/******************************************************************************/
/******************************************************************************/

/* see anydb.h */
int
anydb_transaction(
	anydb_t *db,
	anydb_transaction_t oper
) {
	if (db->itf.transaction)
		return db->itf.transaction(db->clodb, oper);
	return -ENOTSUP;
}

/******************************************************************************/
/******************************************************************************/
/*** FOR ALL                                                                ***/
/******************************************************************************/
/******************************************************************************/

struct for_all_s
{
	anydb_t *db;          /* targeted database */
	time_t now;           /* also drop expired items */
	searchkey_t skey;
	void *closure;
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value);
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

	/* drop expired items */
	if (expired(value->expire, s->now))
		return Anydb_Action_Remove_And_Continue;

	if (searchkey_match(s->db, key, &s->skey)) {
		k.client = string(s->db, key->client);
		k.session = string(s->db, key->session);
		k.user = string(s->db, key->user);
		k.permission = string(s->db, key->permission);
		v.value = string(s->db, value->value);
		v.expire = value->expire;
		s->callback(s->closure, &k, &v);
	}
	return Anydb_Action_Continue;
}

/* see anydb.h */
void
anydb_for_all(
	anydb_t *db,
	void (*callback)(
		void *closure,
		const data_key_t *key,
		const data_value_t *value),
	void *closure,
	const data_key_t *key
) {
	struct for_all_s s;

	if (!searchkey_prepare_match(db, key, &s.skey, false))
		return; /* nothing to do! because one of the idx doesn't exist */

	s.db = db;
	s.closure = closure;
	s.callback = callback;
	s.now = time(NULL);
	db->itf.apply(db->clodb, for_all_cb, &s);
}

/******************************************************************************/
/******************************************************************************/
/*** DROP                                                                   ***/
/******************************************************************************/
/******************************************************************************/

/* structure for dropping items */
struct drop_s
{
	anydb_t *db;          /* targeted database */
	time_t now;           /* also drop expired items */
	searchkey_t skey;     /* the search key */
};

/* callback for dropping items */
static
anydb_action_t
drop_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	struct drop_s *s = closure;

	/* drop expired items */
	if (expired(value->expire, s->now))
		return Anydb_Action_Remove_And_Continue;

	/* remove if matches the key */
	if (searchkey_match(s->db, key, &s->skey))
		return Anydb_Action_Remove_And_Continue;

	/* continue to next */
	return Anydb_Action_Continue;
}

/* see anydb.h */
void
anydb_drop(
	anydb_t *db,
	const data_key_t *key
) {
	struct drop_s s;

	if (!searchkey_prepare_match(db, key, &s.skey, false))
		return; /* nothing to do! because one of the idx doesn't exist */

	s.db = db;
	s.now = time(NULL);
	db->itf.apply(db->clodb, drop_cb, &s);
}

/******************************************************************************/
/******************************************************************************/
/*** SET                                                                    ***/
/******************************************************************************/
/******************************************************************************/

/* structure for setting values */
struct set_s
{
	anydb_t *db;          /* targeted database */
	time_t now;           /* also drop expired items */
	searchkey_t skey;     /* searching key */
	anydb_value_t value;  /* value to set */
};

/* callback for setting values */
static
anydb_action_t
set_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	struct set_s *s = closure;

	/* drop expired items */
	if (expired(value->expire, s->now))
		return Anydb_Action_Remove_And_Continue;

	if (searchkey_is(s->db, key, &s->skey)) {
		value->value = s->value.value;
		value->expire = s->value.expire;
		s->db = NULL; /* indicates that is found */
		return Anydb_Action_Update_And_Stop;
	}

	return Anydb_Action_Continue;
}

/* see anydb.h */
int
anydb_set(
	anydb_t *db,
	const data_key_t *key,
	const data_value_t *value
) {
	int rc;
	struct set_s s;

	rc = searchkey_prepare_is(db, key, &s.skey, true);
	if (rc)
		goto error;

	rc = idx(db, &s.value.value, value->value, true);
	if (rc)
		goto error;

	s.db = db;
	s.value.expire = value->expire;
	s.now = time(NULL);
	db->itf.apply(db->clodb, set_cb, &s);
	if (s.db) {
		/* no item to alter so must be added */
		rc = idx(db, &s.skey.key.permission, key->permission, true);
		if (!rc)
			rc = db->itf.add(db->clodb, &s.skey.key, &s.value);
	}
error:
	return rc;
}

/******************************************************************************/
/******************************************************************************/
/*** TEST                                                                   ***/
/******************************************************************************/
/******************************************************************************/

/* structure for testing rule */
struct test_s
{
	anydb_t *db;          /* targeted database */
	time_t now;
	unsigned score;
	searchkey_t skey;
	anydb_value_t value;
};

/* callback for testing rule */
static
anydb_action_t
test_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	struct test_s *s = closure;
	unsigned sc;

	/* drop expired items */
	if (expired(value->expire, s->now))
		return Anydb_Action_Remove_And_Continue;

	sc = searchkey_test(s->db, key, &s->skey);
	if (sc > s->score) {
		s->score = sc;
		s->value = *value;
	}
	return Anydb_Action_Continue;
}

/* see anydb.h */
unsigned
anydb_test(
	anydb_t *db,
	const data_key_t *key,
	data_value_t *value
) {
	struct test_s s;

	searchkey_prepare_test(db, key, &s.skey, true);

	s.db = db;
	s.now = time(NULL);
	s.score = 0;
	db->itf.apply(db->clodb, test_cb, &s);
	if (s.score) {
		value->value = string(db, s.value.value);
		value->expire = s.value.expire;
	}
	return s.score;
}


/******************************************************************************/
/******************************************************************************/
/*** IS EMPTY                                                               ***/
/******************************************************************************/
/******************************************************************************/

/* structure for testing emptyness */
struct empty_s
{
	bool empty;
	time_t now;
};

/* callback for computing if empty */
static
anydb_action_t
is_empty_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	struct empty_s *s = closure;

	/* drop expired items */
	if (expired(value->expire, s->now))
		return Anydb_Action_Remove_And_Continue;

	s->empty = false;
	return Anydb_Action_Stop;
}

/* see anydb.h */
bool
anydb_is_empty(
	anydb_t *db
) {
	struct empty_s s;

	s.empty = true;
	s.now = time(NULL);
	db->itf.apply(db->clodb, is_empty_cb, &s);
	return s.empty;
}

/******************************************************************************/
/******************************************************************************/
/*** CLEANUP                                                                ***/
/******************************************************************************/
/******************************************************************************/

/* cleanup callback */
static
anydb_action_t
cleanup_cb(
	void *closure,
	const anydb_key_t *key,
	anydb_value_t *value
) {
	return expired(value->expire, *(time_t*)closure)
		? Anydb_Action_Remove_And_Continue : Anydb_Action_Continue;
}

/* see anydb.h */
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
/*** SYNCHRONIZE                                                            ***/
/******************************************************************************/
/******************************************************************************/

/* see anydb.h */
int
anydb_sync(
	anydb_t *db
) {
	return db->itf.sync ? db->itf.sync(db->clodb) : 0;
}

/******************************************************************************/
/******************************************************************************/
/*** DESTROY                                                                ***/
/******************************************************************************/
/******************************************************************************/

/* see anydb.h */
void
anydb_destroy(
	anydb_t *db
) {
	db->itf.destroy(db->clodb);
}
