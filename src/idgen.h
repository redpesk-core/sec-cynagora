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
/******************************************************************************/
/******************************************************************************/
/* HANDLE STRING IDS                                                          */
/******************************************************************************/
/******************************************************************************/

typedef char idgen_t[7];

extern
void
idgen_init(
	idgen_t idgen
);

extern
void
idgen_next(
	idgen_t idgen
);

extern
bool
idgen_is_valid(
	idgen_t idgen
);
