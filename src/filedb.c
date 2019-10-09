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
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF DATABASE WITH FILE BACKEND                               */
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
#include "fbuf.h"
#include "filedb.h"

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

	/**  expiration as a couple of uint32 to ensure compacity */
	uint32_t expire[2];
};
typedef struct rule rule_t;

/*
 * The cynagora database is made of 2 memory mapped files:
 *  - names: the zero terminated names
 *  - rules: the rules based on name indexes as 32bits indexes
 * These files are normally in /var/lib/cynagora
 */
#if !defined(DEFAULT_DB_DIR)
#    define  DEFAULT_DB_DIR  "/var/lib/cynagora"
#endif
#if !defined(DEFAULT_DB_NAME)
#    define  DEFAULT_DB_NAME  "cynagora"
#endif
static const char filedb_default_directory[] = DEFAULT_DB_DIR;
static const char filedb_default_name[] = DEFAULT_DB_NAME;

/** identification of names version 1
 *    $> uuidgen --sha1 -n @url -N urn:AGL:cynagora:db:names:1
 *    $> uuid -v 5 ns:URL urn:AGL:cynagora:db:names:1
 */
static const char uuid_names_v1[] = "b2c33494-995f-5cc2-9e5e-72ad412936a9\n--\n";

/** identification of rules version 1
 *    $> uuidgen --sha1 -n @url -N urn:AGL:cynagora:db:rules:1
 *    $> uuid -v 5 ns:URL urn:AGL:cynagora:db:rules:1
 */
static const char uuid_rules_v1[] = "73630c61-89a9-5e82-8b07-5e53eee785c8\n--\n";

/** length of the identifications */
static const uint32_t uuidlen = 40;


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
	anydb_t anydb;
};
typedef struct filedb filedb_t;

/**
 * Set the expiration of rule to the value
 *
 * @param rule the rule to set
 * @param value the value to set
 */
static
void
set_expire(
	rule_t *rule,
	time_t value
) {
	*(int64_t*)rule->expire = (int64_t)value;
}

/**
 * Get the expiration of rule
 *
 * @param rule the rule to set
 * @return the expiration
 */
static
time_t
get_expire(
	rule_t *rule
) {
	return (time_t)*(int64_t*)rule->expire;
}

/**
 * Return the name of the given index
 * @param filedb the database handler
 * @param index index of the string MUST be valid
 * @return the name for the index
 */
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

/**
 * Initialize the fields 'names_sorted' and 'names_count' for the
 * current database.
 * @param filedb the database handler
 * @return 0 in case of success or -ENOMEM or -ENOEXEC
 */
static
int
init_names(
	filedb_t *filedb
) {
	uint32_t pos, length, *sorted, *p, allocated, name_count;

	allocated = 0;
	name_count = 0;
	sorted = NULL;

	/* iterate over names */
	pos = uuidlen;
	while (pos < filedb->fnames.used) {
		/* get name length */
		length = (uint32_t)strlen(name_at(filedb, pos));
		if (pos + length <= pos || pos + length > filedb->fnames.used) {
			/* overflow */
			free(sorted);
			fprintf(stderr, "bad file %s\n", filedb->fnames.name);
			return -ENOEXEC;
		}
		/* store the position */
		if (allocated <= name_count) {
			allocated += 1024;
			p = realloc(sorted, allocated * sizeof *sorted);
			if (p == NULL) {
				free(sorted);
				fprintf(stderr, "out of memory\n");
				return -ENOMEM;
			}
			sorted = p;
		}
		sorted[name_count++] = pos;
		/* next */
		pos += length + 1;
	}

	/* sort and record */
	qsort_r(sorted, name_count, sizeof *sorted, cmpnames, filedb);
	filedb->names_sorted = sorted;
	filedb->names_count = name_count;
	return 0;
}

/**
 * Initialize the fields 'rules' and 'rules_count' for the
 * current database.
 * @param filedb the database handler
 */
static
void
init_rules(
	filedb_t *filedb
) {
	filedb->rules = (rule_t*)(filedb->frules.buffer + uuidlen);
	filedb->rules_count = (filedb->frules.used - uuidlen) / sizeof *filedb->rules;
}

/**
 * Open the fbuf 'fb' in the directory, the name and the extension.
 * Check that the identifier prefix matches or if the file doesn't exist
 * create the prefix.
 * @param fb the buffer to open
 * @param directory the directory containing the file
 * @param name the basename for the file
 * @param extension the extension of the file
 * @param id the identifier prefix
 * @param idlen the length of the identifier prefix
 * @return 0 in case of success
 *         -ENOMEM if out of memory
 *         -ENOKEY if identification failed
 *         a negative -errno code
 */
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
	char *file, *p;
	size_t ldir, lext, lname;

	/* compute sizes */
	ldir = strlen(directory);
	lname = strlen(name);
	lext = strlen(extension);

	/* allocate memory for file */
	file = alloca((ldir + lname + lext) + 3);

	/* make the file's name: directory/name.extension */
	p = mempcpy(file, directory, ldir);
	*p++ = '/';
	p = mempcpy(p, name, lname);
	*p++ = '.';
	mempcpy(p, extension, lext + 1);

	/* open the fbuf now */
	return fbuf_open_identify(fb, file, NULL, id, idlen);
}

/**
 * Open the database of 'name' in 'directory'
 * @param filedb the database handler to open
 * @param directory the directory containing the database (or null for default)
 * @param name the basename for the file
 * @return 0 in case of success
 *         -ENOMEM if out of memory
 *         -ENOKEY if identification failed
 *         a negative -errno code
 */
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
	rc = open_identify(&filedb->fnames, directory, name, "names", uuid_names_v1, uuidlen);
	if (rc == 0) {
		/* open the rules */
		rc = open_identify(&filedb->frules, directory, name, "rules", uuid_rules_v1, uuidlen);
		if (rc == 0) {
			/* connect internals */
			rc = init_names(filedb);
			if (rc == 0) {
				init_rules(filedb);
				return 0;
			}
			fbuf_close(&filedb->frules);
		}
		fbuf_close(&filedb->fnames);
	}
	return rc;
}

/**
 * Close the database
 * @param filedb database to close
 */
static
void
closedb(
	filedb_t *filedb
) {
	assert(filedb->fnames.name && filedb->frules.name);
	fbuf_close(&filedb->fnames);
	fbuf_close(&filedb->frules);
}

/**
 * Synchronize database and its files (write it to the filesystem)
 * @param filedb database to synchronize
 * @return 0 in case of success
 *         a negative -errno code
 */
static
int
syncdb(
	filedb_t *filedb
) {
	int rc;

	assert(filedb->fnames.name && filedb->frules.name);
	if (!filedb->is_changed)
		rc = 0; /* unchanged */
	else {
		/* sync the names */
		rc = fbuf_sync(&filedb->fnames);
		if (rc == 0) {
			/* sync the rules */
			rc = fbuf_sync(&filedb->frules);
			if (rc == 0) {
				filedb->is_changed = false;
				filedb->has_backup = false;
			}
		}
	}
	return rc;
}

/**
 * Creates backups of the database
 * @param filedb the database to backup
 * @return 0 in case of success
 *         a negative -errno code
 */
static
int
backupdb(
	filedb_t *filedb
) {
	int rc;

	assert(filedb->fnames.name && filedb->frules.name);
	if (filedb->has_backup)
		rc = 0; /* already backuped */
	else {
		/* backup names */
		rc = fbuf_backup(&filedb->fnames);
		if (rc == 0) {
			/* backup rules */
			rc = fbuf_backup(&filedb->frules);
			if (rc == 0)
				filedb->has_backup = true;
		}
	}
	return rc;
}

/**
 * recover the database from latest backup
 * @param filedb database to recover
 * @return 0 in case of success
 *         a negative -errno code
 */
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
		/* recover names */
		rc = fbuf_recover(&filedb->fnames);
		if (rc < 0)
			goto error;

		/* recover rules */
		rc = fbuf_recover(&filedb->frules);
		if (rc < 0)
			goto error;

		/* init names */
		rc = init_names(filedb);
		if (rc < 0)
			goto error;

		init_rules(filedb);
		filedb->is_changed = false;
		filedb->need_cleanup = false;
	}
	return rc;
error:
	fprintf(stderr, "db recovery impossible: %m\n");
	exit(5);
	return rc;
}

/** implementation of anydb_itf.index */
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
			fprintf(stderr, "out of memory\n");
			return -1;
		}
		filedb->names_sorted = p;
	}
	memmove(&filedb->names_sorted[lo + 1], &filedb->names_sorted[lo], (up - lo) * sizeof *filedb->names_sorted);
	filedb->names_count = up + 1;
	*idx = filedb->names_sorted[lo] = i;
	return 0;
}

/** implementation of anydb_itf.string */
static
const char *
string_itf(
	void *clodb,
	anydb_idx_t idx
) {
	filedb_t *filedb = clodb;

	assert(idx < filedb->fnames.used);
	return name_at(filedb, idx);
}

/** implementation of anydb_itf.apply */
static
void
apply_itf(
	void *clodb,
	anydb_applycb_t *oper,
	void *closure
) {
	filedb_t *filedb = clodb;
	anydb_action_t a;
	rule_t *rule;
	anydb_key_t key;
	anydb_value_t value;
	uint32_t i, saved;

	key.session = AnyIdx_Wide;
	i = 0;
	while (i < filedb->rules_count) {
		rule = &filedb->rules[i];
		key.client = rule->client;
		key.user = rule->user;
		key.permission = rule->permission;
		value.value = rule->value;
		value.expire = get_expire(rule);
		a = oper(closure, &key, &value);
		if (a & Anydb_Action_Remove) {
			*rule = filedb->rules[--filedb->rules_count];
			filedb->is_changed = true;
			filedb->need_cleanup = true;
			saved = (uint32_t)((void*)rule - filedb->frules.buffer);
			if (saved < filedb->frules.saved)
				filedb->frules.saved = saved;
			filedb->frules.used -= (uint32_t)sizeof *rule;
		} else if (a & Anydb_Action_Update) {
			rule->value = value.value;
			set_expire(rule, value.expire);
			filedb->need_cleanup = true;
			filedb->is_changed = true;
			saved = (uint32_t)((void*)rule - filedb->frules.buffer);
			if (saved < filedb->frules.saved)
				filedb->frules.saved = saved;
		}
		if (a & Anydb_Action_Stop)
			return;
		i += !(a & Anydb_Action_Remove);
	}
}

/** implementation of anydb_itf.transaction */
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
	default:
		errno = EINVAL;
		rc = -1;
		break;
	}
	return rc;
}

/** implementation of anydb_itf.add */
static
int
add_itf(
	void *clodb,
	const anydb_key_t *key,
	const anydb_value_t *value
) {
	filedb_t *filedb = clodb;
	int rc;
	rule_t *rules;
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
	set_expire(rules, value->expire);
	filedb->frules.used = alloc;
	filedb->is_changed = true;
	return 0;
}

/**
 * Search (dig) dichotomically in 'array' of 'count' elements the index of
 * 'item'. Store '*index' either the index of the found item or the index
 * where inserting the item. Return true if found of false otherwise.
 * @param array array to dig
 * @param count count of elements in array
 * @param item item to dig
 * @param index where to store the found index
 * @return true if found of false otherwise.
 */
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

/**
 * Add dichotomically the 'item' in the 'array' of 'count' elements
 * @param array array to alter
 * @param count count of element in the input array
 * @param item the item to add
 * @return the new count of elements
 */
static
uint32_t
gc_add(
	uint32_t *array,
	uint32_t count,
	uint32_t item
) {
	uint32_t index, i;

	/* search the item */
	if (gc_dig(array, count, item, &index))
		return count; /* already in */

	/* shift the elemetns above index */
	i = count;
	while (i > index) {
		array[i] = array[i - 1];
		i = i - 1;
	}

	/* add the item */
	array[i] = item;
	return count + 1;
}

/**
 * Mark in 'array' of 'count' elements the 'item'
 * @param array the sorted array of marked items
 * @param count the count of marked items
 * @param item the item to mark
 * @return the new count of marked items
 */
static
uint32_t
gc_mark(
	uint32_t *array,
	uint32_t count,
	uint32_t item
) {
	return item > AnyIdx_Max ? count : gc_add(array, count, item);
}

/**
 * Test if 'item' is marked in 'array' of 'count' marked elements
 * @param array the sorted array of marked items
 * @param count the count of marked items
 * @param item the item to search
 * @param index where to store the index of the item if found
 * @return true is found (marked) or false otherwise (not marked)
 */
static
bool
gc_is_marked(
	uint32_t *array,
	uint32_t count,
	uint32_t item,
	uint32_t *index
) {
	return item <= AnyIdx_Max && gc_dig(array, count, item, index);
}

/**
 * Translate the item pointed by 'item' to its new value after renumeration
 * @param marked the sorted array of marked items
 * @param renum the renumerotation of the marked items
 * @param count the count of marked items
 * @param item the pointer to the item to modify
 */
static
void
gc_renum(
	uint32_t *marked,
	uint32_t *renum,
	uint32_t count,
	uint32_t *item
) {
	uint32_t index;

	if (gc_is_marked(marked, count, *item, &index))
		*item = renum[index];
}

/** implementation of anydb_itf.gc */
static
void
gc_itf(
	void *clodb
) {
	filedb_t *filedb = clodb;
	uint32_t rule_count;
	uint32_t name_count;
	rule_t *rules;
	uint32_t *marked;
	uint32_t *renum;
	char *strings;
	uint32_t irule, new_count, imarked, istr_before, istr_after, lenz;

	/* check cleanup required */
	if (!filedb->need_cleanup)
		return;
	filedb->need_cleanup = false;

	/* mark items */
	rule_count = filedb->rules_count;
	name_count = filedb->names_count;
	rules = filedb->rules;
	marked = alloca(name_count * sizeof *marked);
	new_count = 0;
	for (irule = 0 ; irule < rule_count ; irule++) {
		new_count = gc_mark(marked, new_count, rules[irule].client);
		new_count = gc_mark(marked, new_count, rules[irule].user);
		new_count = gc_mark(marked, new_count, rules[irule].permission);
		new_count = gc_mark(marked, new_count, rules[irule].value);
	}

	/* pack if too much unused */
	if (new_count + (new_count >> 2) >= name_count)
		return;

	/* pack the names by removing the unused strings */
	strings = (char*)filedb->fnames.buffer;
	renum = filedb->names_sorted;
	istr_before = istr_after = uuidlen;
	while (istr_before < filedb->fnames.used) {
		/* get name length */
		lenz = 1 + (uint32_t)strlen(strings + istr_before);
		if (gc_is_marked(marked, new_count, istr_before, &imarked)) {
			renum[imarked] = istr_after;
			if (istr_before != istr_after)
				memcpy(strings + istr_after, strings + istr_before, lenz);
			istr_after += lenz;
		}
		/* next */
		istr_before += lenz;
	}

	/* renum the rules */
	for (irule = 0 ; irule < rule_count ; irule++) {
		gc_renum(marked, renum, new_count, &rules[irule].client);
		gc_renum(marked, renum, new_count, &rules[irule].user);
		gc_renum(marked, renum, new_count, &rules[irule].permission);
		gc_renum(marked, renum, new_count, &rules[irule].value);
	}

	/* record and sort */
	filedb->names_count = new_count;
	filedb->fnames.used = istr_after;
	qsort_r(renum, new_count, sizeof *renum, cmpnames, filedb);

	/* set as changed */
	filedb->is_changed = true;
}

/** implementation of anydb_itf.sync */
static
int
sync_itf(
	void *clodb
) {
	filedb_t *filedb = clodb;
	return syncdb(filedb);
}

/** implementation of anydb_itf.destroy */
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

/**
 * Initialize the anydb interface of filedb
 * @param filedb the structure to initialize
 */
static
void
init_anydb_itf(
	filedb_t *filedb
) {
	filedb->anydb.clodb = filedb;

	filedb->anydb.itf.index = index_itf;
	filedb->anydb.itf.string = string_itf;
	filedb->anydb.itf.transaction = transaction_itf;
	filedb->anydb.itf.apply = apply_itf;
	filedb->anydb.itf.add = add_itf;
	filedb->anydb.itf.gc = gc_itf;
	filedb->anydb.itf.sync = sync_itf;
	filedb->anydb.itf.destroy = destroy_itf;
}

/* see filedb.h */
int
filedb_create(
	anydb_t **adb,
	const char *directory,
	const char *basename
) {
	int rc;
	filedb_t *filedb;

	/* allocates */
	*adb = NULL;
	filedb = calloc(1, sizeof *filedb);
	if (!filedb)
		return -ENOMEM;

	/* init anydb interface */
	init_anydb_itf(filedb);

	/* open the database file */
	rc = opendb(filedb, directory, basename);
	if (rc)
		free(filedb);
	else
		*adb = &filedb->anydb;
	return rc;
}

