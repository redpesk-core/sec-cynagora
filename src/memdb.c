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

#define RBS 20
#define SBS 30

struct rule
{
	anydb_key_t key;
	anydb_value_t value;
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
		a = oper(closure, &rules[ir].key, &rules[ir].value);
		switch (a) {
		case Anydb_Action_Continue:
			ir++;
			break;
		case Anydb_Action_Update_And_Stop:
			return;
		case Anydb_Action_Remove_And_Continue:
			rules[ir] = rules[--memdb->rules.count];
			break;
		}
	}
}

static
int
add_itf(
	void *clodb,
	const anydb_key_t *key,
	const anydb_value_t *value
) {
	memdb_t *memdb = clodb;
	struct rule *rules = memdb->rules.values;

	if (memdb->rules.count == memdb->rules.alloc) {
		rules = realloc(rules, (memdb->rules.alloc + RBS) * sizeof *rules);
		if (!rules)
			return -ENOMEM;
		memdb->rules.alloc += RBS;
		memdb->rules.values = rules;
	}
	rules[memdb->rules.count].key = *key;
	rules[memdb->rules.count].value = *value;
	memdb->rules.count++;
	return 0;
}

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
		renum[rules[i].key.client] = 1;
		renum[rules[i].key.session] = 1;
		renum[rules[i].key.user] = 1;
		renum[rules[i].key.permission] = 1;
		renum[rules[i].value.value] = 1;
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
			rules[i].key.client = renum[rules[i].key.client];
			rules[i].key.session = renum[rules[i].key.session];
			rules[i].key.user = renum[rules[i].key.user];
			rules[i].key.permission = renum[rules[i].key.permission];
			rules[i].value.value = renum[rules[i].value.value];
		}
	}

	i = memdb->strings.alloc;
	while (ns + SBS > i)
		i -= SBS;
	if (i != memdb->strings.alloc) {
		memdb->strings.alloc = i;
		memdb->strings.values = realloc(strings, i * sizeof *strings);
	}

	i = memdb->rules.alloc;
	while (ns + RBS > i)
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
	memdb->db.itf.apply = apply_itf;
	memdb->db.itf.add = add_itf;
	memdb->db.itf.gc = gc_itf;
	memdb->db.itf.destroy = destroy_itf;

	memdb->strings.alloc = 0;
	memdb->strings.count = 0;
	memdb->strings.values = NULL;

	memdb->rules.alloc = 0;
	memdb->rules.count = 0;
	memdb->rules.values = NULL;
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
