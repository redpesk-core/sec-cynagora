/*
 * Copyright (C) 2018-2021 IoT.bzh Company
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
/* CONVERTION OF EXPIRATIONS TO AND FROM TEXT                                 */
/******************************************************************************/
/******************************************************************************/

#include <stdbool.h>
#include <string.h>

#include "idgen.h"

static char i2c[] =
	"0123456789"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"abcdefghijklmnopqrstuvwxyz"
	"-+*/<%$#@?!,.&~^>=|_"
;
static char nxt[96];

#define ZERO i2c[0]
#define ONE i2c[1]

static char next(char c)
{
	unsigned i;
	char r;

	if ((signed char)c <= 32)
		r = ONE;
	else {
		r = nxt[c - 32];
		if (!r) {
			memset(nxt, ONE, sizeof(nxt));
			for (i = 0 ; (r = i2c[i]) ; i++)
				nxt[r - 32] = i2c[i + 1] ?: ZERO;
			r = nxt[c - 32];
		}
	}
	return r;
}

void
idgen_init(
	idgen_t idgen
) {
	memset(idgen, 0, sizeof(idgen_t));
	idgen[0] = ZERO;
}

void
idgen_next(
	idgen_t idgen
) {
	unsigned i;
	char c;

	i = 0;
	do {
		c = next(idgen[i]);
		idgen[i++] = c;
	} while (c == ZERO && i < sizeof(idgen_t) - 1);
}

bool
idgen_is_valid(
	idgen_t idgen
) {
	unsigned i = 0;
	while (i < sizeof(idgen_t) && idgen[i] && strchr(i2c, idgen[i]))
		i++;
	return i && i < sizeof(idgen_t) && !idgen[i];
}
