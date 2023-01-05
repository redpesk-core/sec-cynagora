/*
 * Copyright (C) 2018-2023 IoT.bzh Company
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
/* IMPLEMENTATION OF SERVER PART OF CYNAGORA-PROTOCOL                         */
/******************************************************************************/
/******************************************************************************/

typedef struct cyn_server cyn_server_t;

/**
 * Boolean flag telling whether the server logs or not its received commands
 */
extern
bool
cyn_server_log;

/**
 * Create a cynagora server
 * 
 * @param server where to store the handler of the created server
 * @param admin_socket_spec specification of the admin socket
 * @param check_socket_spec specification of the check socket
 * @param agent_socket_spec specification of the agent socket
 * 
 * @return 0 on success or a negative -errno value
 * 
 * @see cyn_server_destroy
 */
extern
int
cyn_server_create(
	cyn_server_t **server,
	const char *admin_socket_spec,
	const char *check_socket_spec,
	const char *agent_socket_spec
);

/**
 * Destroy a created server and release its resources
 * 
 * @param server the handler of the server
 * 
 * @see cyn_server_create
 */
extern
void
cyn_server_destroy(
	cyn_server_t *server
);

/**
 * Start the cynagora server and returns only when stopped
 * 
 * @param server the handler of the server
 * 
 * @return 0 on success or a negative -errno value
 * 
 * @see cyn_server_stop
 */
extern
int
cyn_server_serve(
	cyn_server_t *server
);

/**
 * Stop the cynagora server
 * 
 * @param server the handler of the server
 * @param status the status that the function cyn_server_serve should return
 * 
 * @see cyn_server_serve
 */
extern
void
cyn_server_stop(
	cyn_server_t *server,
	int status
);

