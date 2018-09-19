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


#define _GNU_SOURCE


#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "fbuf.h"
#include "db.h"

#define NOEXPIRE 0
#define NOIDX   0

#define ANYIDX  40
#define ANYSTR  "#"

#define WIDEIDX 42
#define WIDESTR "*"

/*
 * for the first version,  save enougth time up to 4149
 * 4149 = 1970 + (4294967296 * 16) / (365 * 24 * 60 * 60)
 *
 * in the next version, time will be relative to a stored base
 */
#define exp2time(x)  (((time_t)(x)) << 4)
#define time2expl(x) ((uint32_t)((x) >> 4))
#define time2exph(x) time2expl((x) + 15)

/**
 * A rule is a set of 32 bits integers
 */
struct rule
{
	union {
		uint32_t ids[5];
		struct {
			/** client string id */
			uint32_t client;

			/** session string id */
			uint32_t session;

			/** user string id */
			uint32_t user;

			/** permission string id */
			uint32_t permission;

			/** value string id */
			uint32_t value;
		};
	};

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
static const char db_default_directory[] = DEFAULT_DB_DIR;

/** the file for the names */
static fbuf_t fnames;

/** the file for the rules */
static fbuf_t frules;

/** identification of names version 1 (uuidgen --sha1 -n @url -N urn:AGL:cynara:db:names:1) */
static const char uuid_names_v1[] = "e9481f9e-b2f4-5716-90cf-c286d98d1868\n--\n";

/** identification of rules version 1 (uuidgen --sha1 -n @url -N urn:AGL:cynara:db:rules:1) */
static const char uuid_rules_v1[] = "8f7a5b21-48b1-57af-96c9-d5d7192be370\n--\n";

/** length of the identification */
static const int uuidlen = 40;

/** count of names */
static uint32_t names_count;

/** the name indexes sorted */
static uint32_t *names_sorted;

/** count of rules */
static uint32_t rules_count;

/** the rules */
static rule_t *rules;

/** return the name of 'index' */
static
const char*
name_at(
	uint32_t index
) {
	return (const char*)(fnames.buffer + index);
}

/** compare names. used by qsort and bsearch */
static
int
cmpnames(
	const void *pa,
	const void *pb
) {
	uint32_t a = *(const uint32_t*)pa;
	uint32_t b = *(const uint32_t*)pb;
	return strcmp(name_at(a), name_at(b));
}

/** search the index of 'name' and create it if 'needed' */
int
db_get_name_index(
	uint32_t *index,
	const char *name,
	bool needed
) {
	uint32_t lo, up, m, i, *p;
	int c;
	const char *n;
	size_t len;

	/* special names */
	if (!name || !name[0])
		name = ANYSTR;

	/* dichotomic search */
	lo = 0;
	up = names_count;
	while(lo < up) {
		m = (lo + up) >> 1;
		i = names_sorted[m];
		n = name_at(i);
		c = strcmp(n, name);

		if (c == 0) {
			/* found */
			*index = i;
			return 0;
		}

		/* dichotomic iteration */
		if (c < 0)
			lo = m + 1;
		else
			up = m;
	}

	/* not found */
	if (!needed) {
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
	i = fnames.used;
	c = fbuf_append(&fnames, name, 1 + (uint32_t)len);
	if (c < 0)
		return c;

	/* add the name in sorted array */
	up = names_count;
	if (!(up & 1023)) {
		p = realloc(names_sorted, (up + 1024) * sizeof *names_sorted);
		if (p == NULL) {
			fprintf(stderr, "out of memory");
			return -1;
		}
		names_sorted = p;
	}
	memmove(&names_sorted[lo + 1], &names_sorted[lo], (up - lo) * sizeof *names_sorted);
	names_count = up + 1;
	*index = names_sorted[lo] = i;
	return 0;
}

/** initialize names */
static
int
init_names(
) {
	int rc;
	uint32_t pos, len, *ns, *p, all, nc;

	all = 0;
	nc = 0;
	ns = NULL;

	/* iterate over names */
	pos = uuidlen;
	while (pos < fnames.saved) {
		/* get name length */
		len = (uint32_t)strlen(name_at(pos));
		if (pos + len <= pos || pos + len > fnames.saved) {
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
	qsort(ns, nc, sizeof *ns, cmpnames);
	names_sorted = ns;
	names_count = nc;

	/* predefined symbols */
	rc = db_get_name_index(&pos, ANYSTR, true);
	if (rc < 0)
		goto error;
	if (pos != ANYIDX)
		goto bad_file;
	rc = db_get_name_index(&pos, WIDESTR, true);
	if (rc < 0)
		goto error;
	if (pos != WIDEIDX)
		goto bad_file;

	return 0;
bad_file:
	fprintf(stderr, "bad file %s", fnames.name);
	errno = ENOEXEC;
error:
	return -1;
}

/** check whether the 'text' fit ANYSTR, NULL or ""  */
static
bool
is_any(
	const char *text
) {
	return text == NULL || text[0] == 0 || 0 == strcmp(text, ANYSTR);
}

/** check whether the 'text' fit ANYSTR, WIDESTR, NULL or ""  */
static
bool
is_any_or_wide(
	const char *text
) {
	return is_any(text) || 0 == strcmp(text, WIDESTR);
}

/** set the 'value' to the rule at 'index' */
static
void
touch_at(
	uint32_t index
) {
	uint32_t pos;

	pos = (uint32_t)(((void*)&rules[index]) - frules.buffer);
	if (pos < frules.saved)
		frules.saved = pos;
}

/** set the 'value' to the rule at 'index' */
static
void
set_at(
	uint32_t index,
	uint32_t value,
	uint32_t expire
) {
	assert(index < rules_count);
	rules[index].value = value;
	rules[index].expire = expire;
	touch_at(index);
}

/** drop the rule at 'index' */
static
void
drop_at(
	uint32_t index
) {
	uint32_t pos;

	assert(index < rules_count);
	if (index < --rules_count)
		rules[index] = rules[rules_count];
	pos = (uint32_t)(((void*)&rules[rules_count]) - frules.buffer);
	frules.used = pos;
	touch_at(index);
}

/** add the rule 'client' x 'session' x 'user' x 'permission' x 'value' */
static
int
add_rule(
	uint32_t client,
	uint32_t session,
	uint32_t user,
	uint32_t permission,
	uint32_t value,
	uint32_t expire
) {
	int rc;
	uint32_t c;
	rule_t *rule;

	c = frules.used + (uint32_t)sizeof *rule;
	rc = fbuf_ensure_capacity(&frules, c);
	if (rc)
		return rc;
	rules = (rule_t*)(frules.buffer + uuidlen);
	rule = &rules[rules_count++];
	rule->client = client;
	rule->session = session;
	rule->user = user;
	rule->permission = permission;
	rule->value = value;
	rule->expire = expire;
	frules.used = c;
	return 0;
}

/** init the rules from the file */
static
void
init_rules(
) {
	rules = (rule_t*)(frules.buffer + uuidlen);
	rules_count = (frules.used - uuidlen) / sizeof *rules;
}

/** open a fbuf */
static
int
open_identify(
	fbuf_t	*fb,
	const char *directory,
	const char *name,
	const char *id,
	uint32_t idlen
) {
	int rc;
	char *file, *backup;

	rc = asprintf(&file, "%s/%s", directory, name);
	if (rc < 0)
		rc = -ENOMEM;
	else {
		rc = asprintf(&backup, "%s~", file);
		if (rc < 0)
			rc = -ENOMEM;
		else {
			rc = fbuf_open_identify(fb, file, backup, id, idlen);
			free(backup);
		}
		free(file);
	}
	return rc;
}

/** open the database for files 'names' and 'rules' (can be NULL) */
int
db_open(
	const char *directory
) {
	int rc;

	/* provide default directory */
	if (directory == NULL)
		directory = db_default_directory;

	/* open the names */
	rc = open_identify(&fnames, directory, "cynara.names", uuid_names_v1, uuidlen);
	if (rc < 0)
		goto error;

	/* open the rules */
	rc = open_identify(&frules, directory, "cynara.rules", uuid_rules_v1, uuidlen);
	if (rc < 0)
		goto error;

	/* connect internals */
	rc = init_names();
	if (rc < 0)
		goto error;

	init_rules();
	return 0;
error:
	return -1;
}

/** close the database */
void
db_close(
) {
	assert(fnames.name && frules.name);
	fbuf_close(&fnames);
	fbuf_close(&frules);
}

/** is the database empty */
bool
db_is_empty(
) {
	return !rules_count;
}

/** synchronize db on files */
int
db_sync(
) {
	int rc;

	assert(fnames.name && frules.name);
	rc = fbuf_sync(&fnames);
	if (rc == 0)
		rc = fbuf_sync(&frules);
	return rc;
}

/** make a backup of the database */
int
db_backup(
) {
	int rc;

	assert(fnames.name && frules.name);
	rc = fbuf_backup(&fnames);
	if (rc == 0)
		rc = fbuf_backup(&frules);
	return rc;
}

/** recover the database from latest backup */
int
db_recover(
) {
	int rc;

	assert(fnames.name && frules.name);

	rc = fbuf_recover(&fnames);
	if (rc < 0)
		goto error;

	rc = fbuf_recover(&frules);
	if (rc < 0)
		goto error;

	rc = init_names();
	if (rc < 0)
		goto error;

	init_rules();
	return 0;
error:
	fprintf(stderr, "db recovering impossible: %m");
	exit(5);
	return rc;
}

/** enumerate */
void
db_for_all(
	void *closure,
	void (*callback)(
		void *closure,
		const char *client,
		const char *session,
		const char *user,
		const char *permission,
		const char *value,
		time_t expire),
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	uint32_t ucli, uusr, uses, i;
	bool anyperm;

	if (db_get_name_index(&ucli, client, false)
	 || db_get_name_index(&uses, session, false)
	 || db_get_name_index(&uusr, user, false))
		return; /* nothing to do! */

	anyperm = is_any(permission);
	for (i = 0; i < rules_count; i++) {
		if ((ucli == ANYIDX || ucli == rules[i].client)
		 && (uses == ANYIDX || uses == rules[i].session)
		 && (uusr == ANYIDX || uusr == rules[i].user)
		 && (anyperm || !strcasecmp(permission, name_at(rules[i].permission)))) {
			callback(closure,
				name_at(rules[i].client),
				name_at(rules[i].session),
				name_at(rules[i].user),
				name_at(rules[i].permission),
				name_at(rules[i].value),
				exp2time(rules[i].expire));
		}
	}
}

/** drop rules */
int
db_drop(
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	uint32_t ucli, uses, uusr, i;
	bool anyperm;

	if (db_get_name_index(&ucli, client, false)
	 || db_get_name_index(&uses, session, false)
	 || db_get_name_index(&uusr, user, false))
		return 0; /* nothing to do! */

	anyperm = is_any(permission);
	i = 0;
	while (i < rules_count) {
		if ((ucli == ANYIDX || ucli == rules[i].client)
		 && (uses == ANYIDX || uses == rules[i].session)
		 && (uusr == ANYIDX || uusr == rules[i].user)
		 && (anyperm || !strcasecmp(permission, name_at(rules[i].permission))))
			drop_at(i);
		else
			i++;
	}
	return 0;
}

/** set rules */
int
db_set(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char *value,
	time_t expire
) {
	int rc;
	uint32_t ucli, uses, uusr, uperm, uval, i;

	/* normalize */
	client = is_any_or_wide(client) ? WIDESTR : client;
	session = is_any_or_wide(session) ? WIDESTR : session;
	user = is_any_or_wide(user) ? WIDESTR : user;
	permission = is_any_or_wide(permission) ? WIDESTR : permission;

	/* get/create strings */
	rc = db_get_name_index(&ucli, client, true);
	if (rc)
		goto error;
	rc = db_get_name_index(&uses, session, true);
	if (rc)
		goto error;
	rc = db_get_name_index(&uusr, user, true);
	if (rc)
		goto error;
	rc = db_get_name_index(&uval, value, true);
	if (rc)
		goto error;

	/* search the existing rule */
	for (i = 0; i < rules_count; i++) {
		if (ucli == rules[i].client
		 && uses == rules[i].session
		 && uusr == rules[i].user
		 && !strcasecmp(permission, name_at(rules[i].permission))) {
			/* found */
			set_at(i, uval, time2exph(expire));
			return 0;
		}
	}

	/* create the rule */
	rc = db_get_name_index(&uperm, permission, true);
	if (rc)
		goto error;

	rc = add_rule(ucli, uses, uusr, uperm, uval, time2exph(expire));

	return 0;
error:
	return rc;
}

/** check rules */
int
db_test(
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char **value,
	time_t *expire
) {
	uint32_t ucli, uses, uusr, i, score, sc, now;
	rule_t *rule, *found;

	/* check */
	client = is_any_or_wide(client) ? WIDESTR : client;
	session = is_any_or_wide(session) ? WIDESTR : session;
	user = is_any_or_wide(user) ? WIDESTR : user;
	permission = is_any_or_wide(permission) ? WIDESTR : permission;

	/* search the items */
	if (db_get_name_index(&ucli, client, false))
		ucli = NOIDX;
	if (db_get_name_index(&uses, session, false))
		uses = NOIDX;
	if (db_get_name_index(&uusr, user, false))
		uusr = NOIDX;

	/* search the existing rule */
	now = time2expl(time(NULL));
	found = NULL;
	score = 0;
	for (i = 0 ; i < rules_count ; i++) {
		rule = &rules[i];
		if ((!rule->expire || rule->expire >= now)
		 && (ucli == rule->client || WIDEIDX == rule->client)
		 && (uses == rule->session || WIDEIDX == rule->session)
		 && (uusr == rule->user || WIDEIDX == rule->user)
		 && (WIDEIDX == rule->permission
			|| !strcasecmp(permission, name_at(rule->permission)))) {
			/* found */
			sc = 1 + (rule->client != WIDEIDX) + (rule->session != WIDEIDX)
				+ (rule->user != WIDEIDX) + (rule->permission != WIDEIDX);
			if (sc > score) {
				score = sc;
				found = rule;
			}
		}
	}
	if (!found) {
		*value = NULL;
		*expire = 0;
		return 0;
	}
	*value = name_at(found->value);
	*expire = exp2time(found->expire);
	return 1;
}

typedef struct gc gc_t;
struct gc
{
	uint32_t *befores;
	uint32_t *afters;
};

/** compare indexes. used by qsort and bsearch */
static
int
cmpidxs(
	const void *pa,
	const void *pb
) {
	uint32_t a = *(const uint32_t*)pa;
	uint32_t b = *(const uint32_t*)pb;
	return a < b ? -1 : a != b;
}

static
void
gc_mark(
	gc_t *gc,
	uint32_t idx
) {
	uint32_t *p = bsearch(&idx, gc->befores, names_count, sizeof *gc->befores, cmpidxs);
	assert(p != NULL);
	gc->afters[p - gc->befores] = 1;
}

static
uint32_t
gc_after(
	gc_t *gc,
	uint32_t idx
) {
	uint32_t *p = bsearch(&idx, gc->befores, names_count, sizeof *gc->befores, cmpidxs);
	assert(p != NULL);
	return gc->afters[p - gc->befores];
}

static
int
gc_init(
	gc_t *gc
) {
	gc->befores = malloc((sizeof *gc->befores + sizeof *gc->afters) * names_count);
	if (gc->befores == NULL)
		return -ENOMEM;

	names_count = names_count;
	memcpy(gc->befores, names_sorted, names_count * sizeof *gc->befores);
	qsort(gc->befores, names_count, sizeof *gc->befores, cmpidxs);
	gc->afters = &gc->befores[names_count];
	memset(gc->afters, 0, names_count * sizeof *gc->afters);
	
	gc_mark(gc, ANYIDX);
	gc_mark(gc, WIDEIDX);
	return 0;
}

static
void
gc_end(
	gc_t *gc
) {
	free(gc->befores);
}

static
int
gc_pack(
	gc_t *gc
) {
	uint32_t i, j, n, next, prev;
	char *strings;

	/* skip the unchanged initial part */
	n = names_count;
	i = 0;
	while (i < n && gc->afters[i])
		i++;

	/* at end means no change */
	if (i == n)
		return 0;

	/* pack the strings */
	strings = fnames.buffer;
	j = i;
	memcpy(gc->afters, gc->befores, j * sizeof *gc->afters);
	next = gc->befores[i++];
	fnames.saved = next;
	while (i < n) {
		if (gc->afters[i]) {
			gc->befores[j] = prev = gc->befores[i];
			gc->afters[j++] = next;
			while ((strings[next++] = strings[prev++]));
		}
		i++;
	}
	fnames.used = next;
	names_count = j;
	memcpy(names_sorted, gc->afters, j * sizeof *gc->afters);
	qsort(names_sorted, j, sizeof *names_sorted, cmpnames);

	return 1;
}

int
db_cleanup(
) {
	int rc, chg;
	uint32_t idbef, idaft, i, j, now;
	gc_t gc;
	rule_t *rule;

	/* init garbage collector */
	rc= gc_init(&gc);
	if (rc < 0)
		return rc;

	/* default now */
	now = time2expl(time(NULL));

	/* remove expired entries and mark string ids of remaining ones */
	i = 0;
	while (i < rules_count) {
		rule = &rules[i];
		if (rule->expire && now >= rule->expire)
			drop_at(i);
		else {
			for (j = 0 ; j < 5 ; j++)
				gc_mark(&gc, rule->ids[j]);
			i++;
		}
	}

	/* pack the strings */
	if (gc_pack(&gc)) {
		/* replace the ids if changed */
		i = 0;
		while (i < rules_count) {
			rule = &rules[i];
			for (chg = j = 0 ; j < 5 ; j++) {
				idbef = rule->ids[j];
				idaft = gc_after(&gc, idbef);
				rule->ids[j] = idaft;
				chg += idbef != idaft;
			}
			if (chg)
				touch_at(i);
			i++;
		}
	}

	/* terminate */
	gc_end(&gc);

	return 0;
}

