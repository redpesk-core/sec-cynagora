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
/* READING DATABASE RULE FILES FOR INITIALISATION                             */
/******************************************************************************/
/******************************************************************************/

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "data.h"
#include "cyn.h"
#include "expire.h"
#include "db-import.h"

/* see db-import.h */
int db_import_file(FILE *file, const char *location)
{
	int rc, lino;
	char *item[10];
	char buffer[2048];
	data_key_t key;
	data_value_t value;

	/* enter critical section */
	rc = cyn_enter(db_import_file);
	if (rc < 0)
		return rc;

	/* ensure location is not NULL */
	if (location == NULL)
		location = file == stdin ? "<stdin>" : "<unknown file>";

	/* read lines of the file */
	lino = 0;
	while(fgets(buffer, sizeof buffer, file)) {

		/* parse the line */
		lino++;
		item[0] = strtok(buffer, " \t\n\r");
		item[1] = strtok(NULL, " \t\n\r");
		item[2] = strtok(NULL, " \t\n\r");
		item[3] = strtok(NULL, " \t\n\r");
		item[4] = strtok(NULL, " \t\n\r");
		item[5] = strtok(NULL, " \t\n\r");
		item[6] = strtok(NULL, " \t\n\r");

		/* skip empty lines and comments */
		if (item[0] == NULL)
			continue;
		if (item[0][0] == '#')
			continue;

		/* check items of the rule */
		if (item[1] == NULL || item[2] == NULL
		  || item[3] == NULL || item[4] == NULL
		  || item[5] == NULL) {
			fprintf(stderr, "field missing (%s:%d)\n", location, lino);
			rc = -EINVAL;
			goto error;
		}
		if (item[6] != NULL && item[6][0] != '#') {
			fprintf(stderr, "extra field (%s:%d)\n", location, lino);
			rc = -EINVAL;
			goto error;
		}

		/* create the key and value of the rule */
		key.client = item[0];
		key.session = item[1];
		key.user = item[2];
		key.permission = item[3];
		value.value = item[4];
		if (!txt2exp(item[5], &value.expire, true)) {
			fprintf(stderr, "bad expiration %s (%s:%d)\n", item[5], location, lino);
			rc = -EINVAL;
			goto error;
		}

		/* record the rule */
		rc = cyn_set(&key, &value);
		if (rc < 0) {
			fprintf(stderr, "can't set (%s:%d)\n", location, lino);
			exit(1);
		}
	}
	if (!feof(file)) {
		rc = -errno;
		fprintf(stderr, "error while reading file %s\n", location);
		goto error;
	}
	rc = 0;
error:
	if (rc)
		/* cancel changes if error occured */
		cyn_leave(db_import_file, 0);
	else {
		/* commit the changes */
		rc = cyn_leave(db_import_file, 1);
		if (rc < 0)
			fprintf(stderr, "unable to commit content of file %s\n", location);
	}
	return rc;
}

/* see db-import.h */
int db_import_path(const char *path)
{
	int rc;
	FILE *file;

	/* open the file */
	file = fopen(path, "r");
	if (file == NULL) {
		rc = -errno;
		fprintf(stderr, "can't open file %s\n", path);
	}
	else {
		rc = db_import_file(file, path);
		fclose(file);
	}
	return rc;
}
