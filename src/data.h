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
/* GENERIC COMMON DATA TYPES FOR CLIENTS                                      */
/******************************************************************************/
/******************************************************************************/

/** Maximum length of any string */
#define MAX_NAME_LENGTH 8000

/** string for deniying access */
#define DENY    "no"

/** string for allowing access */
#define ALLOW   "yes"

/** default is denying */
#define DEFAULT DENY

/**
 * ANY string, made of one single character, is used to match
 * rules and keys that can contain WIDE or other value.
 * This allow to search specifically to WIDE when WIDE is specified in the
 * search key or to any value (including WIDE) when any is used.
 */
#define Data_Any_Char '#'
#define Data_Any_String "#"

/**
 * WIDE string, made of one character, is used in rules to match any
 * queried value.
 */
#define Data_Wide_Char '*'
#define Data_Wide_String "*"

/**
 * Name of the index on keys
 */
enum data_keyidx {
	KeyIdx_Client,
	KeyIdx_Session,
	KeyIdx_User,
	KeyIdx_Permission,
	KeyIdx_Count
};
typedef enum data_keyidx data_keyidx_t;

/**
 * A key is made of 4 strings that can be accessed by index or by name
 */
union data_key {
	/* name access */
	struct {
		/** the client */
		const char *client;

		/** the session */
		const char *session;

		/** the user */
		const char *user;

		/** the permission */
		const char *permission;
	};
	/** Array for index access, see data_keyidx_t */
	const char *keys[KeyIdx_Count];
};
typedef union data_key data_key_t;

/**
 * A value is made of a string (mainly ALLOW or DENY) and an expiration.
 */
struct data_value {
	/** judgment of the rule: ALLOW, DENY or agent description */
	const char *value;

	/** expiration time of the rule */
	time_t expire;
};
typedef struct data_value data_value_t;
