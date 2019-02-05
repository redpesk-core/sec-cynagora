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

#pragma once

struct prot;
typedef struct prot prot_t;

#define MAXBUFLEN 2000
#define MAXARGS   20
#define FS        ' '
#define RS        '\n'
#define ESC       '\\'


extern
int
prot_create(
	prot_t **prot
);

extern
void
prot_destroy(
	prot_t *prot
);

extern
void
prot_reset(
	prot_t *prot
);

extern
void
prot_put_cancel(
	prot_t *prot
);

extern
int
prot_put_end(
	prot_t *prot
);

extern
int
prot_put_field(
	prot_t *prot,
	const char *field
);

extern
int
prot_put_fields(
	prot_t *prot,
	unsigned count,
	const char **fields
);

extern
int
prot_put(
	prot_t *prot,
	unsigned count,
	const char **fields
);

extern
int
prot_putx(
	prot_t *prot,
	...
);

extern
int
prot_should_write(
	prot_t *prot
);

extern
int
prot_write(
	prot_t *prot,
	int fdout
);

extern
int
prot_can_read(
	prot_t *prot
);

extern
int
prot_read(
	prot_t *prot,
	int fdin
);

extern
int
prot_get(
	prot_t *prot,
	const char ***fields
);

extern
void
prot_next(
	prot_t *prot
);


