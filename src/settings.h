/*
 * Copyright (C) 2018-2026 IoT.bzh Company
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

#if !defined(DEFAULT_CONF_DIR)
#    define  DEFAULT_CONF_DIR        "/etc/security"
#endif
#if !defined(DEFAULT_CONFIG_FILE)
#    define  DEFAULT_CONFIG_FILE     DEFAULT_CONF_DIR"/cynagora.conf"
#endif
#if !defined(DEFAULT_DB_DIR)
#    define  DEFAULT_DB_DIR          "/var/lib/cynagora"
#endif
#if !defined(DEFAULT_INIT_DIR)
#    define  DEFAULT_INIT_DIR        DEFAULT_CONF_DIR"/cynagora.d"
#endif
#if !defined(DEFAULT_CYNAGORA_USER)
#    define  DEFAULT_CYNAGORA_USER   NULL
#endif
#if !defined(DEFAULT_CYNAGORA_GROUP)
#    define  DEFAULT_CYNAGORA_GROUP  NULL
#endif

typedef struct settings_s settings_t;

struct settings_s {
	int makesockdir;
	int makedbdir;
	int owndbdir;
	int ownsockdir;
	int forceinit;
	const char *init;
	const char *dbdir;
	const char *socketdir;
	const char *user;
	const char *group;
};

extern void initialize_default_settings(settings_t *settings);
extern int read_file_settings(settings_t *settings, const char *filename);

