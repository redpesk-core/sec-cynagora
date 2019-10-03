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
/* IMPLEMENTATION OF SERVER PART OF CYNAGORA-PROTOCOL                         */
/******************************************************************************/
/******************************************************************************/

typedef struct cyn_server cyn_server_t;

extern
bool
cyn_server_log;

extern
void
cyn_server_destroy(
	cyn_server_t *server
);

extern
int
cyn_server_create(
	cyn_server_t **server,
	const char *admin_socket_spec,
	const char *check_socket_spec,
	const char *agent_socket_spec
);

extern
int
cyn_server_serve(
	cyn_server_t *server
);

extern
void
cyn_server_stop(
	cyn_server_t *server,
	int status
);

