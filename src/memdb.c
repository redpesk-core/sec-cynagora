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
/* IMPLEMENTATION OF IN MEMORY DATABASE WITHOUT FILE BACKEND                  */
/******************************************************************************/
/******************************************************************************/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "data.h"
#include "anydb.h"
#include "memdb.h"

#define RULE_BLOC_SIZE   20 /**< rule block size */
#define STRING_BLOC_SIZE 30 /**< string bloc size */

#define TAG_CLEAN    0 /**< tag for clean */
#define TAG_DELETED  1 /**< tag for deleted */
#define TAG_CHANGED  2 /**< tag for modified */

/**
 * structure for rules of memory database
 */
struct rule
{
	/** the key */
	anydb_key_t key;

	/** the current value */
	anydb_value_t value;

	/** the next value (depends on tag) */
	anydb_value_t saved;

	/** tag for the value saved */
	uint8_t tag;
};

/**
 * Structure for the memory database
 */
struct memdb
{
	/** first for the fun */
	anydb_t db;

	/** strings */
	struct {
		/** allocated count for strings */
		uint32_t alloc;
		/** used count for strings */
		uint32_t count;
		/** array of strings */
		char **values;
	} strings;

	/** rules */
	struct {
		/** allocated count for rules */
		uint32_t alloc;
		/** used count for rules */
		uint32_t count;
		/** array of rules */
		struct rule *values;
	} rules;

	/** transaction */
	struct {
		/** rule count at the beginning of the transaction */
		uint32_t count;
		/** indicator for an active transaction */
		bool active;
	} transaction;
};
typedef struct memdb memdb_t;

/** implementation of anydb_itf.index */
static
int
index_itf(
	void *clodb,
	anydb_idx_t *idx,
	const char *name,
	bool create
) {
	memdb_t *memdb = clodb;
	char *s, **strings = memdb->strings.values;
	anydb_idx_t i;

	/* search */
	i = 0;
	while (i < memdb->strings.count) {
		if (!strcmp(name, strings[i])) {
			*idx = i;
			return 0;
		}
		i++;
	}

	/* not found */
	if (!create) {
		errno = ENOENT;
		return -1;
	}

	/* create */
	s = strdup(name);
	if (s == NULL)
		return -ENOMEM;
	if (memdb->strings.count == memdb->strings.alloc) {
		strings = realloc(strings, (memdb->strings.alloc
					+ STRING_BLOC_SIZE) * sizeof *strings);
		if (!strings) {
			free(s);
			return -ENOMEM;
		}
		memdb->strings.values = strings;
		memdb->strings.alloc += STRING_BLOC_SIZE;
	}
	i = memdb->strings.count;
	*idx = i;
	strings[i] = s;
	memdb->strings.count = i + 1;
	return 0;
}

/** implementation of anydb_itf.string */
static
const char *
string_itf(
	void *clodb,
	anydb_idx_t idx
) {
	memdb_t *memdb = clodb;

	assert(idx < memdb->strings.count);
	return memdb->strings.values[idx];
}

/** implementation of anydb_itf.apply */
static
void
apply_itf(
	void *clodb,
	anydb_applycb_t *oper,
	void *closure
) {
	memdb_t *memdb = clodb;
	struct rule *rules = memdb->rules.values;
	uint32_t ir;
	anydb_action_t a;

	ir = 0;
	while (ir < memdb->rules.count) {
		if (memdb->transaction.active && rules[ir].tag == TAG_DELETED)
			ir++;
		else {
			a = oper(closure, &rules[ir].key, &rules[ir].value);
			if (a & Anydb_Action_Remove) {
				if (memdb->transaction.active)
					rules[ir++].tag = TAG_DELETED;
				else
					rules[ir] = rules[--memdb->rules.count];
			} else if (a & Anydb_Action_Update) {
				if (memdb->transaction.active)
					rules[ir].tag = TAG_CHANGED;
				else
					rules[ir].saved = rules[ir].value;
			}
			if (a & Anydb_Action_Stop)
				return;
			ir += !(a & Anydb_Action_Remove);
		}
	}
}

/** implementation of anydb_itf.transaction */
static
int
transaction_itf(
	void *clodb,
	anydb_transaction_t oper
) {
	memdb_t *memdb = clodb;
	struct rule *rules;
	uint32_t ir;
	uint32_t count;

	switch (oper) {
	case Anydb_Transaction_Start:
		if (memdb->transaction.active)
			return -EINVAL;
		memdb->transaction.active = true;
		memdb->transaction.count = memdb->rules.count;
		break;
	case Anydb_Transaction_Commit:
		if (!memdb->transaction.active)
			return -EINVAL;
		rules = memdb->rules.values;
		count = memdb->rules.count;
		ir = 0;
		while(ir < count) {
			switch (rules[ir].tag) {
			case TAG_CLEAN:
				ir++;
				break;
			case TAG_DELETED:
				rules[ir] = rules[--count];
				break;
			case TAG_CHANGED:
				rules[ir++].tag = TAG_CLEAN;
				break;
			}
		}
		memdb->rules.count = count;
		memdb->transaction.active = false;
		break;
	case Anydb_Transaction_Cancel:
		if (!memdb->transaction.active)
			return -EINVAL;
		rules = memdb->rules.values;
		count = memdb->rules.count = memdb->transaction.count;
		for (ir = 0 ; ir < count ; ir++) {
			if (rules[ir].tag != TAG_CLEAN) {
				rules[ir].value = rules[ir].saved;
				rules[ir].tag = TAG_CLEAN;
			}
		}
		memdb->transaction.active = false;
		break;
	}
	return 0;
}

/** implementation of anydb_itf.add */
static
int
add_itf(
	void *clodb,
	const anydb_key_t *key,
	const anydb_value_t *value
) {
	memdb_t *memdb = clodb;
	struct rule *rules;
	uint32_t count;
	uint32_t alloc;

	rules = memdb->rules.values;
	count = memdb->rules.count;
	alloc = memdb->rules.alloc;
	if (count == alloc) {
		alloc += RULE_BLOC_SIZE;
		rules = realloc(rules, alloc * sizeof *rules);
		if (!rules)
			return -ENOMEM;
		memdb->rules.alloc = alloc;
		memdb->rules.values = rules;
	}
	rules = &rules[count];
	rules->key = *key;
	rules->saved = rules->value = *value;
	rules->tag = TAG_CLEAN;
	memdb->rules.count = count + 1;
	return 0;
}

/**
 * Mark the 'item' as being used
 * @param renum array handling marked items
 * @param item the item to check
 */
static
void
gc_mark(
	anydb_idx_t *renum,
	anydb_idx_t item
) {
	if (anydb_idx_is_string(item))
		renum[item] = 1;
}

/**
 * return the renumring of 'item' within 'renum'
 * @param renum the renumbering array
 * @param item the item to renumber
 * @return the renumbered item
 */
static
anydb_idx_t
gc_renum(
	anydb_idx_t *renum,
	anydb_idx_t item
) {
	return anydb_idx_is_special(item)? item : renum[item];
}

/** implementation of anydb_itf.gc */
static
void
gc_itf(
	void *clodb
) {
	memdb_t *memdb = clodb;
	uint32_t i, j;
	uint32_t rule_count = memdb->rules.count;
	uint32_t name_count = memdb->strings.count;
	char **strings = memdb->strings.values;
	struct rule *rules = memdb->rules.values;
	anydb_idx_t *renum = alloca(name_count * sizeof *renum);

	/* mark used strings */
	memset(renum, 0, name_count * sizeof *renum);
	for (i = 0 ; i < rule_count ; i++) {
		gc_mark(renum, rules[i].key.client);
		gc_mark(renum, rules[i].key.session);
		gc_mark(renum, rules[i].key.user);
		gc_mark(renum, rules[i].key.permission);
		gc_mark(renum, rules[i].value.value);
	}

	/* pack the used strings */
	for (i = j = 0 ; i < name_count ; i++) {
		if (renum[i]) {
			strings[j] = strings[i];
			renum[i] = j++;
		} else {
			free(strings[i]);
			renum[i] = AnyIdx_Invalid;
		}
	}
	if (name_count != j) {
		/* renumber the items of the database */
		memdb->strings.count = name_count = j;
		for (i = 0 ; i < rule_count ; i++) {
			rules[i].key.client = gc_renum(renum, rules[i].key.client);
			rules[i].key.session = gc_renum(renum, rules[i].key.session);
			rules[i].key.user = gc_renum(renum, rules[i].key.user);
			rules[i].key.permission = gc_renum(renum, rules[i].key.permission);
			rules[i].value.value = gc_renum(renum, rules[i].value.value);
		}
	}

	/* decrease size of array for strings */
	i = memdb->strings.alloc;
	while (name_count + STRING_BLOC_SIZE < i)
		i -= STRING_BLOC_SIZE;
	if (i != memdb->strings.alloc) {
		memdb->strings.alloc = i;
		memdb->strings.values = realloc(strings, i * sizeof *strings);
	}

	/* decrease size of array for rules */
	i = memdb->rules.alloc;
	while (rule_count + RULE_BLOC_SIZE < i)
		i -= RULE_BLOC_SIZE;
	if (i != memdb->rules.alloc) {
		memdb->rules.alloc = i;
		memdb->rules.values = realloc(rules, i * sizeof *rules);
	}
}

/** implementation of anydb_itf.destroy */
static
void
destroy_itf(
	void *clodb
) {
	memdb_t *memdb = clodb;
	if (memdb) {
		free(memdb->strings.values);
		free(memdb->rules.values);
		free(memdb);
	}
}

/**
 * Initialize the structure of the memory database
 * @param memdb the structure to initialize
 */
static
void
init_memdb(
	memdb_t *memdb
) {
	memdb->db.clodb = memdb;

	memdb->db.itf.index = index_itf;
	memdb->db.itf.string = string_itf;
	memdb->db.itf.transaction = transaction_itf;
	memdb->db.itf.apply = apply_itf;
	memdb->db.itf.add = add_itf;
	memdb->db.itf.gc = gc_itf;
	memdb->db.itf.sync = 0;
	memdb->db.itf.destroy = destroy_itf;

	memdb->strings.alloc = 0;
	memdb->strings.count = 0;
	memdb->strings.values = NULL;

	memdb->rules.alloc = 0;
	memdb->rules.count = 0;
	memdb->rules.values = NULL;

	memdb->transaction.count = 0;
	memdb->transaction.active = false;
}

/* see memdb.h */
int
memdb_create(
	anydb_t **memdb
) {
	memdb_t *mdb;

	/* allocate */
	mdb = malloc(sizeof *mdb);
	if (!mdb) {
		*memdb = NULL;
		return -ENOMEM;
	}

	/* init */
	init_memdb(mdb);
	*memdb = &mdb->db;
	return 0;
}
