/*
 * Copyright (C) 2018-2022 IoT.bzh Company
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
/* IMPLEMENTATION OF NAMES                                                    */
/******************************************************************************/
/******************************************************************************/

#include <ctype.h>
#include <string.h>
#include <stdint.h>

#include "names.h"

/**
 * Check the name and compute its length. Returns 0 in case of invalid name
 * @param name the name to check
 * @return the length of the name or zero if invalid
 */
uint8_t
agent_check_name(
	const char *name
) {
	char c;
	uint8_t length = 0;

	if (name) {
		while ((c = name[length])) {
			if (!isalnum(c) && !strchr("@_-$", c)) {
				length = 0;
				break;
			}
			if (!++length)
				break;
		}
	}
	return length;
}

