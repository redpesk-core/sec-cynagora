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

#include "data.h"
#include "cyn.h"

/**
 * Parse the spec to extract the derived key to ask.
 *
 * @param spec   The specification of the derived key
 * @param rkey   The originally requested key
 * @param key    The derived key or NULL for computing length
 * @param buffer The buffer that handles texts or NULL for computing length
 * @param szbuf  The size of the buffer or 0 for computing length
 * @return the total length of buffer used
 */
static
size_t
parse(
	const char *spec,
	const data_key_t *rkey,
	data_key_t *key,
	char *buffer,
	size_t szbuf
) {
	size_t iout, ikey, inf;
	const char *val;

	iout = 0;
	for (ikey = 0 ; ikey < KeyIdx_Count ; ikey++) {
		inf = iout;
		while(*spec) {
			if (*spec == ':' && ikey < 3) {
				/* : is the separator of key's items */
				spec++;
				break; /* next key */
			}
			if (!(*spec == '%' && spec[1])) {
				/* not a % substitution mark */
				if (iout < szbuf)
					buffer[iout] = *spec;
				iout++;
				spec++;
			} else {
				/* what % substitution is it? */
				switch(spec[1]) {
				case 'c':
					val = rkey->client;
					break;
				case 's':
					val = rkey->session;
					break;
				case 'u':
					val = rkey->user;
					break;
				case 'p':
					val = rkey->permission;
					break;
				default:
					/* none */
					val = 0;
				}
				if (val) {
					/* substitution of the value */
					while (*val) {
						if (iout < szbuf)
							buffer[iout] = *val;
						iout++;
						val++;
					}
				} else {
					/* no substitution */
					if (spec[1] != ':' && spec[1] != '%') {
						/* only escape % and : */
						if (iout < szbuf)
							buffer[iout] = '%';
						iout++;
					}
					if (iout < szbuf)
						buffer[iout] = spec[1];
					iout++;
				}
				spec += 2;
			}
		}
		if (inf == iout)
			val = 0; /* empty key item */
		else {
			/* set zero ended key */
			val = &buffer[inf];
			if (iout < szbuf)
				buffer[iout] = 0;
			iout++;
		}
		if (key)
			key->keys[ikey] = val;
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
 * @param on_result_cb       callback that will asynchronously handle the result
 * @param on_result_closure  closure for 'on_result_cb'
 * @return
 */
static
int
agent_at_cb(
	const char *name,
	void *agent_closure,
	const data_key_t *key,
	const char *value,
	on_result_cb_t *on_result_cb,
	void *on_result_closure
) {
	data_key_t atkey;
	char *block;
	size_t size;

	/* compute the length */
	size = parse(value, key, 0, 0, 0);
	/* alloc the length locally */
	block = alloca(size);
	/* initialize the derived key */
	parse(value, key, &atkey, block, size);
	/* ask for the derived key */
	return cyn_test_async(on_result_cb, on_result_closure, &atkey);
}

/* see agent-at.h */
int
agent_at_activate(
) {
	return cyn_agent_add("@", agent_at_cb, 0);
}
