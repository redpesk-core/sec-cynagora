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
#pragma once
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF IN MEMORY DATABASE WITHOUT FILE BACKEND                  */
/******************************************************************************/
/******************************************************************************/

/**
 * Create the object handling the memory database
 * @param memdb pointer to the handling object to return
 */
extern
int
memdb_create(
	anydb_t **memdb
);
