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
/* CONVERTION OF EXPIRATIONS TO AND FROM TEXT                                 */
/******************************************************************************/
/******************************************************************************/

/**
 * Converts the time of the string to an expiration
 *
 * The string code a time relative to now using the format
 * XXXyXXXwXXXdXXXhXXXmXXXs where XXX are numbers.
 *
 * Examples:
 *  - 15 means 15 seconds
 *  - 4h15m30s means 4 hours 15 minutes 30 seconds
 *  - forever means forever
 *  - 2m2w means two months and 2 weeks
 *
 * @param txt the text to convert
 * @param time_out where to store the result
 * @param absolute return the expiration in epoch
 * @return true if valid false otherwise
 */
extern
bool
txt2exp(
	const char *txt,
	time_t *time_out,
	bool absolute
);

/**
 * Converts the expiration in to its relative string representation
 *
 * @param expire the epiration to convert
 * @param expire is expiration absolute?
 * @param buffer the buffer where to store the converted string
 * @param buflen length of the buffer
 * @return the length of the resulting string, can be greater than buflen but
 * in that case, no more than buflen characters are copied to buffer
 */
extern
size_t
exp2txt(
	time_t expire,
	bool absolute,
	char *buffer,
	size_t buflen
);
