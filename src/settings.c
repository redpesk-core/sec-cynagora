/*
 * Copyright (C) 2018-2026 IoT.bzh Company
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
/* READ SETTINGS OF CYNAGORA                                                  */
/******************************************************************************/
/******************************************************************************/
/*
 * settings is a text file
 * comments are starting with #
 * lines are either empty or only with comments
 * or KEY VALUE [# comment]
 * where separators are spaces or tabs
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cyn-protocol.h"
#include "settings.h"

#define SIZE_BUFFER_SETTINGS   1000

typedef struct desc_setting_s desc_setting_t;

struct desc_setting_s {
	const char *key;
	enum { STRING, BOOLEAN } type;
	unsigned offset;
};

static const desc_setting_t dscset[] = {
#define OFFSET(fld)  ((unsigned)offsetof(settings_t, fld))

	{ "init",            STRING,  OFFSET(init) },
	{ "dbdir",           STRING,  OFFSET(dbdir) },
	{ "socketdir",       STRING,  OFFSET(socketdir) },
	{ "user",            STRING,  OFFSET(user) },
	{ "group",           STRING,  OFFSET(group) },
	{ "force-init",      BOOLEAN, OFFSET(forceinit) },
	{ "make-db-dir",     BOOLEAN, OFFSET(makedbdir) },
	{ "make-socket-dir", BOOLEAN, OFFSET(makesockdir) },
	{ "own-db-dir",      BOOLEAN, OFFSET(owndbdir) },
	{ "own-socket-dir",  BOOLEAN, OFFSET(ownsockdir) }
#undef OFFSET
};

static const desc_setting_t *search_key(const char *key, size_t len)
{
	const desc_setting_t *it = dscset;
	const desc_setting_t *end = &dscset[sizeof dscset / sizeof *dscset];

	while (it != end) {
		if (0 == strncmp(it->key, key, len) && '\0' == it->key[len])
			return it;
		it++;
	}
	return NULL;
}

static int read_settings(settings_t *settings, FILE *file)
{
	char cmt = 0;
	const desc_setting_t *dskey;
	size_t lkey, lval, lsp;
	char *str, *key, *val;
	void *pfld;
	char buffer[SIZE_BUFFER_SETTINGS];

	while ((str = fgets(buffer, sizeof buffer, file)) != NULL) {
		while (*str == ' ' || *str == '\t') str++;
		if (!cmt && *str != '#') {
			lkey = strcspn(str, " \t\r\n");
			if (lkey != 0) {
				/* search the key */
				key = str;
				dskey = search_key(key, lkey);
				if (dskey == NULL) {
					fprintf(stderr, "invalid key %.*s\n", (int)lkey, key);
					return -1;
				}
				/* skip spaces */
				str += lkey;
				lsp = strspn(str, " \t\r\n");
				str += lsp;
				/* get value */
				val = str;
				lval = strcspn(str, " \t\r\n");
				if (lval == 0) {
					fprintf(stderr, "no value for key %.*s\n", (int)lkey, key);
					return -1;
				}
				/* check nothing signifiant after value */
				str += lval;
				lsp = strspn(str, " \t\r\n");
				str += lsp;
				if (*str != '\0' && *str != '#') {
					fprintf(stderr, "extra value for key %.*s\n", (int)lkey, key);
					return -1;
				}
				/* set the value */
				pfld = (void*)(dskey->offset + (intptr_t)settings);
				switch (dskey->type) {
				case STRING:
					*(const char**)pfld = strndup(val, lval);
					if (NULL == *(const char**)pfld) {
						fprintf(stderr, "allocation failed for key %.*s\n", (int)lkey, key);
						return -1;
					}
					break;
				case BOOLEAN:
					if (lval == 2 && 0 == strncmp(val, "no", 2))
						*(int*)pfld = 0;
					else if (lval == 3 && 0 == strncmp(val, "yes", 3))
						*(int*)pfld = 1;
					else {
						fprintf(stderr, "bad key value %.*s (expected: yes or no)\n", (int)lval, val);
						return -1;
					}
					break;
				}
			}
		}
		if (cmt || *str == '#')
			do { cmt = *str != '\n'; } while(*++str);
	}
	return -!feof(file);
}

void initialize_default_settings(settings_t *settings)
{
	settings->makesockdir = 0;
	settings->makedbdir = 0;
	settings->owndbdir = 0;
	settings->ownsockdir = 0;
	settings->forceinit = 0;
	settings->init = DEFAULT_INIT_DIR;
	settings->dbdir = DEFAULT_DB_DIR;
	settings->socketdir = cyn_default_socket_dir;
	settings->user = DEFAULT_CYNAGORA_USER;
	settings->group = DEFAULT_CYNAGORA_GROUP;
}

int read_file_settings(settings_t *settings, const char *filename)
{
	int rc;
	FILE *file;

	/* use default file if existing */
	if (filename == NULL) {
		filename = DEFAULT_CONFIG_FILE;
		if (access(filename, F_OK))
			return 0;
	}

	/* read the settings from the file */
	file = fopen(filename, "r");
	if (file == NULL)
		rc = -1;
	else {
		rc = read_settings(settings, file);
		fclose(file);
	}

	/* done */
	return rc;
}

