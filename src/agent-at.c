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
#include <stdbool.h>
#include <time.h>
#include <string.h>

#include "data.h"
#include "cyn.h"

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
	for (ikey = 0 ; ikey < 4 ; ikey++) {
		inf = iout;
		while(*spec) {
			if (*spec == ':' && ikey < 3) {
				spec++;
				break;
			}
			if (*spec == '%' && spec[1]) {
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
					val = 0;
				}
				if (val) {
					while (*val) {
						if (iout < szbuf)
							buffer[iout] = *val;
						iout++;
						val++;
					}
				} else {
					if (spec[1] != ':' && spec[1] != '%') {
						if (iout < szbuf)
							buffer[iout] = '%';
						iout++;
					}
					if (iout < szbuf)
						buffer[iout] = spec[1];
					iout++;
				}
				spec += 2;
			} else {
				if (iout < szbuf)
					buffer[iout] = *spec;
				iout++;
				spec++;
			}
		}
		if (inf == iout)
			val = 0;
		else {
			val = &buffer[inf];
			if (iout < szbuf)
				buffer[iout] = 0;
			iout++;
		}
		if (key)
			((const char**)key)[ikey] = val;
	}
	return iout;
}

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

	size = parse(value, key, 0, 0, 0);
	block = alloca(size);
	parse(value, key, &atkey, block, size);
	return cyn_test_async(on_result_cb, on_result_closure, &atkey);
}

int
agent_at_activate(
) {
	return cyn_agent_add("@", agent_at_cb, 0);
}
