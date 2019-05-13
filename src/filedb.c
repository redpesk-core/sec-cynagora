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
#include "fbuf.h"
#include "filedb.h"

#define MAX_NAME_LENGTH 32768

/*
 * for the first version,  save enougth time up to 4149
 * 4149 = 1970 + (4294967296 * 16) / (365 * 24 * 60 * 60)
 *
 * in the next version, time will be relative to a stored base
 */
#define exp2time(x)  (((time_t)(x)) << 4)
#define time2exp(x)  ((x) ? ((uint32_t)(((x) + 15) >> 4)) : 0)

/**
 * A rule is a set of 32 bits integers
 */
struct rule
{
	/** client string id */
	uint32_t client;

	/** user string id */
	uint32_t user;

	/** permission string id */
	uint32_t permission;

	/** value string id */
	uint32_t value;

	/**  expiration */
	uint32_t expire;
};
typedef struct rule rule_t;

/*
 * The cynara-agl database is made of 2 memory mapped files:
 *  - names: the zero terminated names
 *  - rules: the rules based on name indexes as 32bits indexes
 * These files are normally in /var/lib/cynara
 */
#if !defined(DEFAULT_DB_DIR)
#    define  DEFAULT_DB_DIR  "/var/lib/cynara"
#endif
#if !defined(DEFAULT_DB_NAME)
#    define  DEFAULT_DB_NAME  "cynara"
#endif
static const char filedb_default_directory[] = DEFAULT_DB_DIR;
static const char filedb_default_name[] = DEFAULT_DB_NAME;

/** identification of names version 2
 *    $> uuidgen --sha1 -n @url -N urn:AGL:cynara:db:names:2
 *    $> uuid -v 5 ns:URL urn:AGL:cynara:db:names:2
 */
static const char uuid_names_v2[] = "6fa114d4-f3d9-58ab-a5d3-4674ee865c8d\n--\n";

/** identification of rules version 2
 *    $> uuidgen --sha1 -n @url -N urn:AGL:cynara:db:rules:2
 *    $> uuid -v 5 ns:URL urn:AGL:cynara:db:rules:2
 */
static const char uuid_rules_v2[] = "6d48515a-3f64-52b1-9d15-4d13d073d48a\n--\n";

/** length of the identification */
static const int uuidlen = 40;


struct filedb
{
	/** the file for the names */
	fbuf_t fnames;

	/** the file for the rules */
	fbuf_t frules;

	/** count of names */
	uint32_t names_count;

	/** the name indexes sorted */
	uint32_t *names_sorted;

	/** count of rules */
	uint32_t rules_count;

	/** the rules */
	rule_t *rules;

	/** is changed? */
	bool is_changed;

	/** needs cleanup? */
	bool need_cleanup;

	/** has backup? */
	bool has_backup;

	/** the anydb interface */
	anydb_t db;
};
typedef struct filedb filedb_t;

/** return the name of 'index' */
static
const char*
name_at(
	filedb_t *filedb,
	uint32_t index
) {
	return (const char*)(filedb->fnames.buffer + index);
}

/** compare names. used by qsort and bsearch */
static
int
cmpnames(
	const void *pa,
	const void *pb,
	void *arg
) {
	uint32_t a = *(const uint32_t*)pa;
	uint32_t b = *(const uint32_t*)pb;
	filedb_t *filedb = arg;
	return strcmp(name_at(filedb, a), name_at(filedb, b));
}

/** initialize names */
static
int
init_names(
	filedb_t *filedb
) {
	uint32_t pos, len, *ns, *p, all, nc;

	all = 0;
	nc = 0;
	ns = NULL;

	/* iterate over names */
	pos = uuidlen;
	while (pos < filedb->fnames.used) {
		/* get name length */
		len = (uint32_t)strlen(name_at(filedb, pos));
		if (pos + len <= pos || pos + len > filedb->fnames.used) {
			free(ns);
			goto bad_file;
		}
		/* store the position */
		if (all <= nc) {
			all += 1024;
			p = realloc(ns, all * sizeof *ns);
			if (p == NULL) {
				free(ns);
				fprintf(stderr, "out of memory");
				goto error;
			}
			ns = p;
		}
		ns[nc++] = pos;
		/* next */
		pos += len + 1;
	}

	/* sort and record */
	qsort_r(ns, nc, sizeof *ns, cmpnames, filedb);
	filedb->names_sorted = ns;
	filedb->names_count = nc;
	return 0;

bad_file:
	fprintf(stderr, "bad file %s", filedb->fnames.name);
	errno = ENOEXEC;
error:
	return -1;
}

/** init the rules from the file */
static
void
init_rules(
	filedb_t *filedb
) {
	filedb->rules = (rule_t*)(filedb->frules.buffer + uuidlen);
	filedb->rules_count = (filedb->frules.used - uuidlen) / sizeof *filedb->rules;
}

/** open a fbuf */
static
int
open_identify(
	fbuf_t	*fb,
	const char *directory,
	const char *name,
	const char *extension,
	const char *id,
	uint32_t idlen
) {
	char *file, *backup, *p;
	size_t ldir, lext, lname;

	ldir = strlen(directory);
	lname = strlen(name);
	lext = strlen(extension);
	file = alloca(((ldir + lname + lext) << 1) + 7);
	p = mempcpy(file, directory, ldir);
	*p++ = '/';
	p = mempcpy(p, name, lname);
	*p++ = '.';
	backup = mempcpy(p, extension, lext + 1);
	p = mempcpy(backup, file, ldir + lname + lext + 2);
	*p++ = '~';
	*p = 0;
	return fbuf_open_identify(fb, file, backup, id, idlen);
}

/** open the database for files 'names' and 'rules' (can be NULL) */
static
int
opendb(
	filedb_t *filedb,
	const char *directory,
	const char *name
) {
	int rc;

	/* provide default directory */
	if (directory == NULL)
		directory = filedb_default_directory;

	/* provide default name */
	if (name == NULL)
		name = filedb_default_name;

	/* open the names */
	rc = open_identify(&filedb->fnames, directory, name, "names", uuid_names_v2, uuidlen);
	if (rc < 0)
		goto error;

	/* open the rules */
	rc = open_identify(&filedb->frules, directory, name, "rules", uuid_rules_v2, uuidlen);
	if (rc < 0)
		goto error;

	/* connect internals */
	rc = init_names(filedb);
	if (rc < 0)
		goto error;

	init_rules(filedb);
	return 0;
error:
	return -1;
}

/** close the database */
static
void
closedb(
	filedb_t *filedb
) {
	assert(filedb->fnames.name && filedb->frules.name);
	fbuf_close(&filedb->fnames);
	fbuf_close(&filedb->frules);
}

/** synchronize db on files */
static
int
syncdb(
	filedb_t *filedb
) {
	int rc;

	assert(filedb->fnames.name && filedb->frules.name);
	if (!filedb->is_changed)
		rc = 0;
	else {
		rc = fbuf_sync(&filedb->fnames);
		if (rc == 0) {
			rc = fbuf_sync(&filedb->frules);
			if (rc == 0) {
				filedb->is_changed = false;
				filedb->has_backup = false;
			}
		}
	}
	return rc;
}

/** make a backup of the database */
static
int
backupdb(
	filedb_t *filedb
) {
	int rc;

	assert(filedb->fnames.name && filedb->frules.name);
	if (filedb->has_backup)
		rc = 0;
	else {
		rc = fbuf_backup(&filedb->fnames);
		if (rc == 0) {
			rc = fbuf_backup(&filedb->frules);
			if (rc == 0) {
				filedb->has_backup = true;
				filedb->is_changed = false;
			}
		}
	}
	return rc;
}

/** recover the database from latest backup */
static
int
recoverdb(
	filedb_t *filedb
) {
	int rc;

	assert(filedb->fnames.name && filedb->frules.name);
	if (!filedb->is_changed || !filedb->has_backup)
		rc = 0;
	else {
		rc = fbuf_recover(&filedb->fnames);
		if (rc < 0)
			goto error;

		rc = fbuf_recover(&filedb->frules);
		if (rc < 0)
			goto error;

		rc = init_names(filedb);
		if (rc < 0)
			goto error;

		init_rules(filedb);
		filedb->is_changed = false;
		filedb->need_cleanup = false;
	}
	return rc;
error:
	fprintf(stderr, "db recovery impossible: %m");
	exit(5);
	return rc;
}

static
int
index_itf(
	void *clodb,
	anydb_idx_t *idx,
	const char *name,
	bool create
) {
	filedb_t *filedb = clodb;
	uint32_t lo, up, m, i, *p;
	int c;
	const char *n;
	size_t len;

	/* dichotomic search */
	lo = 0;
	up = filedb->names_count;
	while(lo < up) {
		m = (lo + up) >> 1;
		i = filedb->names_sorted[m];
		n = name_at(filedb, i);
		c = strcmp(n, name);

		if (c == 0) {
			/* found */
			*idx = i;
			return 0;
		}

		/* dichotomic iteration */
		if (c < 0)
			lo = m + 1;
		else
			up = m;
	}

	/* not found */
	if (!create) {
		errno = ENOENT;
		return -1;
	}

	/* check length */
	len = strnlen(name, MAX_NAME_LENGTH + 1);
	if (len > MAX_NAME_LENGTH) {
		errno = EINVAL;
		return -1;
	}

	/* add the name in the file */
	i = filedb->fnames.used;
	c = fbuf_append(&filedb->fnames, name, 1 + (uint32_t)len);
	if (c < 0)
		return c;

	/* add the name in sorted array */
	up = filedb->names_count;
	if (!(up & 1023)) {
		p = realloc(filedb->names_sorted, (up + 1024) * sizeof *p);
		if (p == NULL) {
			fprintf(stderr, "out of memory");
			return -1;
		}
		filedb->names_sorted = p;
	}
	memmove(&filedb->names_sorted[lo + 1], &filedb->names_sorted[lo], (up - lo) * sizeof *filedb->names_sorted);
	filedb->names_count = up + 1;
	*idx = filedb->names_sorted[lo] = i;
	return 0;
}

static
const char *
string_itf(
	void *clodb,
	anydb_idx_t idx
) {
	filedb_t *filedb = clodb;

	return name_at(filedb, idx);
}

static
void
apply_itf(
	void *clodb,
	anydb_action_t (*oper)(void *closure, const anydb_key_t *key, anydb_value_t *value),
	void *closure
) {
	filedb_t *filedb = clodb;
	anydb_action_t a;
	rule_t *rule;
	anydb_key_t key;
	anydb_value_t value;
	uint32_t i;

	key.session = AnyIdx_Wide;
	i = 0;
	while (i < filedb->rules_count) {
		rule = &filedb->rules[i];
		key.client = rule->client;
		key.user = rule->user;
		key.permission = rule->permission;
		value.value = rule->value;
		value.expire = exp2time(rule->expire);
		a = oper(closure, &key, &value);
		switch (a) {
		case Anydb_Action_Stop:
			return;
		case Anydb_Action_Continue:
			i++;
			break;
		case Anydb_Action_Update_And_Stop:
			rule->value = value.value;
			rule->expire = time2exp(value.expire);
			filedb->need_cleanup = true;
			filedb->is_changed = true;
			return;
		case Anydb_Action_Remove_And_Continue:
			*rule = filedb->rules[--filedb->rules_count];
			filedb->is_changed = true;
			filedb->need_cleanup = true;
			filedb->frules.used -= (uint32_t)sizeof *rule;
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
	filedb_t *filedb = clodb;
	int rc;

	switch (oper) {
	case Anydb_Transaction_Start:
		rc = backupdb(filedb);
		break;
	case Anydb_Transaction_Commit:
		rc = syncdb(filedb);
		break;
	case Anydb_Transaction_Cancel:
		rc = recoverdb(filedb);
		break;
	}
	return rc;
}

static
int
add_itf(
	void *clodb,
	const anydb_key_t *key,
	const anydb_value_t *value
) {
	filedb_t *filedb = clodb;
	int rc;
	struct rule *rules;
	uint32_t alloc;
	uint32_t count;

	alloc = filedb->frules.used + (uint32_t)sizeof *rules;
	rc = fbuf_ensure_capacity(&filedb->frules, alloc);
	if (rc)
		return rc;
	rules = (rule_t*)(filedb->frules.buffer + uuidlen);
	filedb->rules = rules;
	count = filedb->rules_count++;
	rules = &rules[count];
	rules->client = key->client;
	rules->user = key->user;
	rules->permission = key->permission;
	rules->value = value->value;
	rules->expire = time2exp(value->expire);
	filedb->frules.used = alloc;
	filedb->is_changed = true;
	return 0;
}

static
bool
gc_dig(
	uint32_t *array,
	uint32_t count,
	uint32_t item,
	uint32_t *index
) {
	uint32_t lo, up, i;

	/* dichotomic search */
	lo = 0;
	up = count;
	while(lo < up) {
		i = (lo + up) >> 1;
		if (array[i] == item) {
			/* found */
			*index = i;
			return true;
		}

		/* dichotomic iteration */
		if (array[i] < item)
			lo = i + 1;
		else
			up = i;
	}
	*index = lo;
	return false;
}

static
uint32_t
gc_add(
	uint32_t *array,
	uint32_t count,
	uint32_t item
) {
	uint32_t index, i;

	if (gc_dig(array, count, item, &index))
		return count;

	i = count;
	while (i > index) {
		array[i] = array[i - 1];
		i = i - 1;
	}
	array[i] = item;
	return count + 1;
}

static
uint32_t
gc_mark(
	uint32_t *array,
	uint32_t count,
	uint32_t item
) {
	return item > AnyIdx_Max ? count : gc_add(array, count, item);
}

static
bool
gc_new(
	uint32_t *array,
	uint32_t count,
	uint32_t item,
	uint32_t *index
) {
	return item > AnyIdx_Max ? false : gc_dig(array, count, item, index);
}

static
void
gc_itf(
	void *clodb
) {
	filedb_t *filedb = clodb;
	uint32_t nr;
	uint32_t nn;
	struct rule *rules;
	uint32_t *used;
	uint32_t *sorted;
	char *strings;
	uint32_t ir, nu, idx, is, ios, lenz;

	/* check cleanup required */
	if (!filedb->need_cleanup)
		return;
	filedb->need_cleanup = false;

	/* mark items */
	nr = filedb->rules_count;
	nn = filedb->names_count;
	rules = filedb->rules;
	used = alloca(nn * sizeof *used);
	nu = 0;
	for (ir = 0 ; ir < nr ; ir++) {
		nu = gc_mark(used, nu, rules[ir].client);
		nu = gc_mark(used, nu, rules[ir].user);
		nu = gc_mark(used, nu, rules[ir].permission);
		nu = gc_mark(used, nu, rules[ir].value);
	}

	/* pack if too much unused */
	if (nu + (nu >> 2) <= nn)
		return;

	/* pack the names */
	strings = (char*)filedb->fnames.buffer;
	sorted = filedb->names_sorted;
	is = ios = uuidlen;
	while (is < filedb->fnames.used) {
		/* get name length */
		lenz = 1 + (uint32_t)strlen(strings + is);
		if (gc_dig(used, nu, is, &idx)) {
			sorted[idx] = ios;
			if (is != ios)
				memcpy(strings + ios, strings + is, lenz);
			ios += lenz;
		}
		/* next */
		is += lenz;
	}

	/* renum the rules */
	for (ir = 0 ; ir < nr ; ir++) {
		if (gc_new(used, nu, rules[ir].client, &idx))
			rules[ir].client = sorted[idx];
		if (gc_new(used, nu, rules[ir].user, &idx))
			rules[ir].user = sorted[idx];
		if (gc_new(used, nu, rules[ir].permission, &idx))
			rules[ir].permission = sorted[idx];
		if (gc_new(used, nu, rules[ir].value, &idx))
			rules[ir].value = sorted[idx];
	}

	/* record and sort */
	filedb->names_count = nu;
	filedb->fnames.used = ios;
	qsort_r(sorted, nu, sizeof *sorted, cmpnames, filedb);

	/* set as changed */
	filedb->is_changed = true;
}

static
int
sync_itf(
	void *clodb
) {
	filedb_t *filedb = clodb;
	return syncdb(filedb);
}

static
void
destroy_itf(
	void *clodb
) {
	filedb_t *filedb = clodb;
	if (filedb) {
		if (filedb->frules.name)
			closedb(filedb);
		free(filedb);
	}
}

static
void
init(
	filedb_t *filedb
) {
	filedb->db.clodb = filedb;

	filedb->db.itf.index = index_itf;
	filedb->db.itf.string = string_itf;
	filedb->db.itf.transaction = transaction_itf;
	filedb->db.itf.apply = apply_itf;
	filedb->db.itf.add = add_itf;
	filedb->db.itf.gc = gc_itf;
	filedb->db.itf.sync = sync_itf;
	filedb->db.itf.destroy = destroy_itf;
}

int
filedb_create(
	anydb_t **adb,
	const char *directory,
	const char *basename
) {
	int rc;
	filedb_t *filedb;

	*adb = NULL;
	filedb = calloc(1, sizeof *filedb);
	if (!filedb)
		return -ENOMEM;

	init(filedb);

	rc = opendb(filedb, directory, basename);
	if (rc)
		free(filedb);
	else
		*adb = &filedb->db;
	return rc;
}

/** synchronize database */
int
anydb_sync(
	anydb_t *db
) {
	return db->itf.sync ? db->itf.sync(db->clodb) : 0;
}
