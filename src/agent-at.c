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
/* IMPLEMENTATION OF AGENT AT (@)                                             */
/******************************************************************************/
/******************************************************************************/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#include "data.h"
#include "cyn.h"

static const char separator = ';';
static const char escape = '%';

/**
 * Parse the spec to extract the derived key to ask.
 *
 * @param spec           The specification of the derived key
 * @param reference_key  The originally requested key of reference
 * @param derived_key    The derived key or NULL for computing length
 * @param buffer         The buffer that handles texts or NULL for computing length
 * @param szbuf          The size of the buffer or 0 for computing length
 * @return the total length of buffer used
 */
static
size_t
parse(
	const char *spec,
	const data_key_t *reference_key,
	data_key_t *derived_key,
	char *buffer,
	size_t szbuf
) {
	size_t iout, ikey, inf;
	const char *val;
	int sub;

	/* iterate through the fields of the key */
	iout = 0;
	for (ikey = 0 ; ikey < KeyIdx_Count ; ikey++) {
		/* compute value of the derived field */
		inf = iout;
		while(*spec) {
			if (*spec == separator && ikey < 3) {
				/* : is the separator of key's items */
				spec++;
				break; /* next key */
			}
			if (!(*spec == escape && spec[1])) {
				/* not a % substitution mark */
				if (iout < szbuf)
					buffer[iout] = *spec;
				iout++;
				spec++;
			} else {
				/* what % substitution is it? */
				switch(spec[1]) {
				case 'c':
					val = reference_key->client;
					sub = 1;
					break;
				case 's':
					val = reference_key->session;
					sub = 1;
					break;
				case 'u':
					val = reference_key->user;
					sub = 1;
					break;
				case 'p':
					val = reference_key->permission;
					sub = 1;
					break;
				default:
					/* none */
					sub = 0;
					break;
				}
				if (!sub) {
					/* no substitution */
					if (spec[1] != separator && spec[1] != escape) {
						/* only escape % and : */
						if (iout < szbuf)
							buffer[iout] = escape;
						iout++;
					}
					if (iout < szbuf)
						buffer[iout] = spec[1];
					iout++;
				} else if (val != NULL) {
					/* substitution of the value */
					while (*val) {
						if (iout < szbuf)
							buffer[iout] = *val;
						iout++;
						val++;
					}
				}
				spec += 2;
			}
		}
		/* standardize the found item*/
		if (inf == iout)
			val = NULL; /* empty key item */
		else {
			/* set zero ended key */
			val = &buffer[inf];
			if (iout < szbuf)
				buffer[iout] = 0;
			iout++;
		}
		/* record the value */
		if (derived_key)
			derived_key->keys[ikey] = val;
	}
	return iout;
}

/**
 * Implementation of the AT-agent callback
 *
 * @param name               name of the agent (not used, should be "@")
 * @param agent_closure      closure of the agent (not used)
 * @param key                the original searched key
 * @param value              the value found (string after @:)
 * @param query              the query identifer for replying or subquerying
 * @return 0 in case of success or -errno like negative error code
 */
static
int
agent_at_cb(
	const char *name,
	void *agent_closure,
	const data_key_t *key,
	const char *value,
	cynagora_query_t *query
) {
	data_key_t atkey;
	char *block;
	size_t size;

	/* compute the length */
	size = parse(value, key, NULL, NULL, 0);

	/* alloc the length locally */
	block = alloca(size);

	/* initialize the derived key */
	parse(value, key, &atkey, block, size);

	/* ask for the derived key */
	return cyn_query_subquery_async(query, (on_result_cb_t*)cyn_query_reply, query, &atkey);
}

/* see agent-at.h */
int
agent_at_activate(
) {
	return cyn_agent_add("@", agent_at_cb, 0);
}
