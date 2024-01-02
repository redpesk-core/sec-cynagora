/*
 * Copyright (C) 2018-2024 IoT.bzh Company
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
/* READING DATABASE RULE FILES FOR INITIALISATION                             */
/******************************************************************************/
/******************************************************************************/

/**
 * Add to the database the data from file of 'path'
 *
 * ---------------------------------------------------------------------------
 * The file must be made of lines being either empty, comment or rule
 *
 * Empty lines or comment lines are ignored.
 *
 * An empty line only contains spaces and/or tabs
 *
 * A comment line start with the character #. It can be preceded by any count
 * of spaces and/or tabs.
 *
 * Other lines are rules. They must be made of 6 fields separated by any count
 * of space and/or tabs. Spaces and tabs at the begining of the line are
 * ignored.
 *
 * The 6 fields of a rule are:
 *
 *       CLIENT SESSION USER PERMISSION VALUE EXPIRATION
 *
 * CLIENT, SESSION, USER, PERMISSION are arbitrary strings. The single star
 * mean 'any value'.
 *
 * Value must be a string of some meaning. Known values are:
 *
 *   - yes:   grant the permission
 *   - no:    forbid the permission
 *   - @:...: agent at (@)
 *
 * Expiration can be expressed
 *
 * ---------------------------------------------------------------------------
 *
 * @param path path of the initialization file
 * @return 0 in case of success or a negative -errno like code
 */
extern
int
dbinit_add_file(
	const char *path
);

