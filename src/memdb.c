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

#define RBS 20 /**< rule block size */
#define SBS 30 /**< string bloc size */

#define TCLE 0 /**< tag for clean */
#define TDEL 1 /**< tag for deleted */
#define TMOD 2 /**< tag for modified */

struct rule
{
	anydb_key_t key;
	anydb_value_t value;
	anydb_value_t saved;
	uint8_t tag;
};

struct memdb
{
	/* first for the fun */
	anydb_t db;

	/* strings */
	struct {
		uint32_t alloc;
		uint32_t count;
		char **values;
	} strings;

	/* rules */
	struct {
		uint32_t alloc;
		uint32_t count;
		struct rule *values;
	} rules;

	struct {
		uint32_t count;
		bool active;
	} transaction;
};
typedef struct memdb memdb_t;

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
		strings = realloc(strings, (memdb->strings.alloc + SBS) * sizeof *strings);
		if (!strings) {
			free(s);
			return -ENOMEM;
		}
		memdb->strings.values = strings;
		memdb->strings.alloc += SBS;
	}
	i = memdb->strings.count;
	*idx = i;
	strings[i] = s;
	memdb->strings.count = i + 1;
	return 0;
}

static
const char *
string_itf(
	void *clodb,
	anydb_idx_t idx
) {
	memdb_t *memdb = clodb;

	return memdb->strings.values[idx];
}

static
void
apply_itf(
	void *clodb,
	anydb_action_t (*oper)(void *closure, const anydb_key_t *key, anydb_value_t *value),
	void *closure
) {
	memdb_t *memdb = clodb;
	struct rule *rules = memdb->rules.values;
	uint32_t ir;
	anydb_action_t a;

	ir = 0;
	while (ir < memdb->rules.count) {
		if (memdb->transaction.active && rules[ir].tag == TDEL)
			a = Anydb_Action_Continue;
		else
			a = oper(closure, &rules[ir].key, &rules[ir].value);
		switch (a) {
		case Anydb_Action_Stop:
			return;
		case Anydb_Action_Continue:
			ir++;
			break;
		case Anydb_Action_Update_And_Stop:
			if (memdb->transaction.active)
				rules[ir].tag = TMOD;
			else
				rules[ir].saved = rules[ir].value;
			return;
		case Anydb_Action_Remove_And_Continue:
			if (memdb->transaction.active)
				rules[ir++].tag = TDEL;
			else
				rules[ir] = rules[--memdb->rules.count];
			break;
		}
	}
}

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
			case TCLE:
				ir++;
				break;
			case TDEL:
				rules[ir] = rules[--count];
				break;
			case TMOD:
				rules[ir++].tag = TCLE;
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
			if (rules[ir].tag != TCLE) {
				rules[ir].value = rules[ir].saved;
				rules[ir].tag = TCLE;
			}
		}
		memdb->transaction.active = false;
		break;
	}
	return 0;
}

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
		alloc += RBS;
		rules = realloc(rules, alloc * sizeof *rules);
		if (!rules)
			return -ENOMEM;
		memdb->rules.alloc = alloc;
		memdb->rules.values = rules;
	}
	rules = &rules[count];
	rules->key = *key;
	rules->saved = rules->value = *value;
	rules->tag = TCLE;
	memdb->rules.count = count + 1;
	return 0;
}

static
void
gc_mark(
	anydb_idx_t *renum,
	anydb_idx_t item
) {
	if (item <= AnyIdx_Max)
		renum[item] = 1;
}

static
anydb_idx_t
gc_new(
	anydb_idx_t *renum,
	anydb_idx_t item
) {
	return item > AnyIdx_Max ? item : renum[item];
}
#include <stdio.h>
static
void
gc_itf(
	void *clodb
) {
	memdb_t *memdb = clodb;
	uint32_t nr = memdb->rules.count;
	uint32_t ns = memdb->strings.count;
	char **strings = memdb->strings.values;
	struct rule *rules = memdb->rules.values;
	anydb_idx_t *renum = alloca(ns * sizeof *renum);
	uint32_t i, j;

	for (i = 0 ; i < ns ; i++)
		renum[i] = 0;

	for (i = 0 ; i < nr ; i++) {
		gc_mark(renum, rules[i].key.client);
		gc_mark(renum, rules[i].key.session);
		gc_mark(renum, rules[i].key.user);
		gc_mark(renum, rules[i].key.permission);
		gc_mark(renum, rules[i].value.value);
	}

	for (i = j = 0 ; i < ns ; i++) {
		if (renum[i]) {
			strings[j] = strings[i];
			renum[i] = j++;
		} else {
			free(strings[i]);
			renum[i] = AnyIdx_Invalid;
		}
	}
	if (ns != j) {
		memdb->strings.count = ns = j;
		for (i = 0 ; i < nr ; i++) {
			rules[i].key.client = gc_new(renum, rules[i].key.client);
			rules[i].key.session = gc_new(renum, rules[i].key.session);
			rules[i].key.user = gc_new(renum, rules[i].key.user);
			rules[i].key.permission = gc_new(renum, rules[i].key.permission);
			rules[i].value.value = gc_new(renum, rules[i].value.value);
		}
	}

	i = memdb->strings.alloc;
	while (ns + SBS < i)
		i -= SBS;
	if (i != memdb->strings.alloc) {
		memdb->strings.alloc = i;
		memdb->strings.values = realloc(strings, i * sizeof *strings);
	}

	i = memdb->rules.alloc;
	while (nr + RBS < i)
		i -= RBS;
	if (i != memdb->rules.alloc) {
		memdb->rules.alloc = i;
		memdb->rules.values = realloc(rules, i * sizeof *strings);
	}
}

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

static
void
init(
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

int
memdb_create(
	anydb_t **memdb
) {
	memdb_t *mdb;

	mdb = malloc(sizeof *mdb);
	if (!mdb) {
		*memdb = NULL;
		return -ENOMEM;
	}
	init(mdb);
	*memdb = &mdb->db;
	return 0;
}
