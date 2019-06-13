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

#define DENY    "no"
#define ALLOW   "yes"
#define ASK     "ask"
#define DEFAULT DENY

#define Data_Any_Char '#'
#define Data_Wide_Char '*'

#define Data_Any_String "#"
#define Data_Wide_String "*"

typedef struct data_key data_key_t;
typedef struct data_value data_value_t;

enum data_keyidx {
	KeyIdx_Client,
	KeyIdx_Session,
	KeyIdx_User,
	KeyIdx_Permission,
	KeyIdx_Count
};

struct data_key {
	union {
		struct {
			const char *client;
			const char *session;
			const char *user;
			const char *permission;
		};
		const char *keys[KeyIdx_Count];
	};
};

struct data_value {
	const char *value;
	time_t expire;
};

