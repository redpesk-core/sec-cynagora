
#define _GNU_SOURCE


#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>

#include "fbuf.h"
#include "db.h"

#define NOIDX   0

#define ANYIDX  40
#define ANYSTR  "#"

#define WIDEIDX 42
#define WIDESTR "*"

/**
 * A rule is a set of 4 integers
 */
struct rule
{
	uint32_t client, user, permission, value;
};
typedef struct rule rule_t;

/**
 * Sessions
 */
struct session
{
	struct session *next, *prev;
	rule_t *rules;
	const char *name;
	uint32_t count;
};
typedef struct session session_t;

/*
 * The cynara-agl database is made of 2 memory mapped files:
 *  - names: the zero terminated names
 *  - rules: the rules based on name indexes as 32bits indexes
 * These files are normally in /var/lib/cynara
 */

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

/** the sessions */
static session_t sessions = {
	.next = &sessions,
	.prev = &sessions,
	.name = WIDESTR
};

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
			syslog(LOG_ERR, "out of memory");
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
				syslog(LOG_ERR, "out of memory");
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
	syslog(LOG_ERR, "bad file %s", fnames.name);
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

/** get in 'session' the session for 'name' and create it if 'needed' */
static
int
get_session(
	const char *name,
	bool needed,
	session_t **session
) {
	session_t *s;
	size_t len;

	/* start on ANY sessions */
	s = &sessions;
	if (is_any_or_wide(name))
		goto found;

	/* look to other sessions */
	s = s->next;
	while(s != &sessions) {
		if (!strcmp(s->name, name))
			goto found;
		s = s->next;
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

	/* create it */
	s = malloc(sizeof * s + len + 1);
	if (s == NULL)
		return -1; /* out of memory */

	/* init new session */
	s->rules = NULL;
	s->count = 0;
	s->name = strcpy((char*)(s + 1), name);
	s->next = &sessions;
	s->prev = sessions.prev;
	sessions.prev = s;
	s->prev->next = s;
found:
	*session = s;
	return 0;
}

/** for 'session' set the value the rule at 'index' */
static
void
session_set_at(
	session_t *session,
	uint32_t index,
	uint32_t value
) {
	uint32_t pos;

	assert(index < session->count);
	session->rules[index].value = value;
	if (session == &sessions) {
		pos = (uint32_t)(((void*)&session->rules[index]) - frules.buffer);
		if (pos < frules.saved)
			frules.saved = pos;
	}
}

/** drop of 'session' the rule at 'index' */
static
void
session_drop_at(
	session_t *session,
	uint32_t index
) {
	uint32_t pos;

	assert(index < session->count);
	if (index < --session->count)
		session->rules[index] = session->rules[session->count];
	if (session == &sessions) {
		pos = (uint32_t)(((void*)&session->rules[index]) - frules.buffer);
		if (pos < frules.saved)
			frules.saved = pos;
		pos = (uint32_t)(((void*)&session->rules[session->count]) - frules.buffer);
		frules.used = pos;
	}
}

/** add to 'session' the rule 'client' x 'user' x 'permission' x 'value' */
static
int
session_add(
	session_t *session,
	uint32_t client,
	uint32_t user,
	uint32_t permission,
	uint32_t value
) {
	int rc;
	uint32_t c;
	rule_t *rule;

	if (session == &sessions) {
		c = frules.used + (uint32_t)sizeof *rule;
		rc = fbuf_ensure_capacity(&frules, c);
		if (rc)
			return rc;
		frules.used = c;
		session->rules = (rule_t*)(frules.buffer + uuidlen);
	} else {
		c = session->count + 32 - (session->count & 31);
		rule = realloc(session->rules, c * sizeof *rule);
		if (rule == NULL)
			return -ENOMEM;
		session->rules = rule;
	}
	rule = &session->rules[session->count++];
	rule->client = client;
	rule->user = user;
	rule->permission = permission;
	rule->value = value;
	return 0;
}

/** init the rules from the file */
static
void
init_rules(
) {
	sessions.rules = (rule_t*)(frules.buffer + uuidlen);
	sessions.count = (frules.used - uuidlen) / sizeof *sessions.rules;
}

/** open the database for files 'names' and 'rules' (can be NULL) */
int
db_open(
	const char *names,
	const char *rules
) {
	int rc;

	/* open the names */
	rc = fbuf_open_identify(&fnames, names ?: "cynara.names", uuid_names_v1, uuidlen);
	if (rc < 0)
		goto error;

	/* open the rules */
	rc = fbuf_open_identify(&frules, rules ?: "cynara.rules", uuid_rules_v1, uuidlen);
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
	return !sessions.count;
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
		uint32_t value),
	const char *client,
	const char *session,
	const char *user,
	const char *permission
) {
	uint32_t ucli, uusr, i;
	bool anyperm, anysession;
	session_t *ses;

	if (db_get_name_index(&ucli, client, false)
	 || db_get_name_index(&uusr, user, false))
		return; /* nothing to do! */

	anyperm = is_any(permission);
	anysession = is_any(session);
	if (anysession)
		ses = &sessions;
	else {
		if (get_session(session, false, &ses))
			return; /* ignore if no session */
	}
	for(;;) {
		for (i = 0; i < ses->count; i++) {
			if ((ucli == ANYIDX || ucli == ses->rules[i].client)
			 && (uusr == ANYIDX || uusr == ses->rules[i].user)
			 && (anyperm || !strcasecmp(permission, name_at(ses->rules[i].permission)))) {
				callback(closure,
					name_at(ses->rules[i].client),
					ses->name,
					name_at(ses->rules[i].user),
					name_at(ses->rules[i].permission),
					ses->rules[i].value);
			}
		}
		if (!anysession)
			break;
		ses = ses->next;
		if (ses == &sessions)
			break;
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
	uint32_t ucli, uusr, i;
	bool anyperm, anysession;
	session_t *ses;

	if (db_get_name_index(&ucli, client, false)
	 || db_get_name_index(&uusr, user, false))
		return 0; /* nothing to do! */

	anyperm = is_any(permission);
	anysession = is_any(session);
	if (anysession)
		ses = &sessions;
	else {
		if (get_session(session, false, &ses))
			return 0; /* ignore if no session */
	}
	for(;;) {
		i = 0;
		while (i < ses->count) {
			if ((ucli == ANYIDX || ucli == ses->rules[i].client)
			 && (uusr == ANYIDX || uusr == ses->rules[i].user)
			 && (anyperm || !strcasecmp(permission, name_at(ses->rules[i].permission))))
				session_drop_at(ses, i);
			else
				i++;
		}
		if (!anysession)
			break;
		ses = ses->next;
		if (ses == &sessions)
			break;
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
	uint32_t value
) {
	int rc;
	uint32_t ucli, uusr, uperm, i;
	session_t *ses;

	/* normalize */
	client = is_any_or_wide(client) ? WIDESTR : client;
	session = is_any_or_wide(session) ? WIDESTR : session;
	user = is_any_or_wide(user) ? WIDESTR : user;
	permission = is_any_or_wide(permission) ? WIDESTR : permission;

	/* get the session */
	rc = get_session(session, true, &ses);
	if (rc)
		goto error;

	/* get/create strings */
	rc = db_get_name_index(&ucli, client, true);
	if (rc)
		goto error;
	rc = db_get_name_index(&uusr, user, true);
	if (rc)
		goto error;

	/* search the existing rule */
	for (i = 0 ; i < ses->count ; i++) {
		if (ucli == ses->rules[i].client
		 && uusr == ses->rules[i].user
		 && !strcasecmp(permission, name_at(ses->rules[i].permission))) {
			/* found */
			session_set_at(ses, i, value);
			return 0;
		}
	}

	/* create the rule */
	rc = db_get_name_index(&uperm, permission, true);
	if (rc)
		goto error;

	rc = session_add(ses, ucli, uusr, uperm, value);

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
	uint32_t *value
) {
	uint32_t ucli, uusr, i, val, score, sc;
	session_t *ses;
	rule_t *rule;

	/* check */
	client = is_any_or_wide(client) ? WIDESTR : client;
	session = is_any_or_wide(session) ? WIDESTR : session;
	user = is_any_or_wide(user) ? WIDESTR : user;
	permission = is_any_or_wide(permission) ? WIDESTR : permission;

	/* search the items */
	val = score = 0;
#define NOIDX   0
	if (db_get_name_index(&ucli, client, false))
		ucli = NOIDX;
	if (db_get_name_index(&uusr, user, false))
		uusr = NOIDX;

	/* get the session */
	if (get_session(session, false, &ses))
		ses = &sessions;

retry:
	/* search the existing rule */
	for (i = 0 ; i < ses->count ; i++) {
		rule = &ses->rules[i];
		if ((ucli == rule->client || WIDEIDX == rule->client)
		 && (uusr == rule->user || WIDEIDX == rule->user)
		 && (WIDEIDX == rule->permission
			|| !strcasecmp(permission, name_at(rule->permission)))) {
			/* found */
			sc = 1 + (rule->client != WIDEIDX)
				+ (rule->user != WIDEIDX) + (rule->permission != WIDEIDX);
			if (sc > score) {
				score = sc;
				val = rule->value;
			}
		}
	}
	if (!score && ses != &sessions) {
		ses = &sessions;
		goto retry;
	}

	if (score)
		*value = val;
	return score > 0;
}

