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

/*
cynara_admin_initialize(&m_CynaraAdmin),
cynara_admin_finish(m_CynaraAdmin);
cynara_admin_set_policies(m_CynaraAdmin, pp_policies.data()),
cynara_admin_list_policies(m_CynaraAdmin, bucketName.c_str(), appId.c_str(),
cynara_admin_erase(m_CynaraAdmin, bucketName.c_str(), static_cast<int>(recursive),
cynara_admin_check(m_CynaraAdmin, bucket.c_str(), recursive, label.c_str(),

cynara_initialize(&m_Cynara, nullptr),
cynara_finish(m_Cynara);
cynara_check(m_Cynara,
*/
#define _GNU_SOURCE

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#include <cynara/cynara-admin.h>
#include <cynara/cynara-client.h>
#include <cynara/cynara-client-async.h>
#include <cynara/cynara-creds-commons.h>

#ifndef CYNARA_ADMIN_ASK
# define CYNARA_ADMIN_ASK 11
#endif

#include "rcyn-client.h"

/******************** ADMIN ********************************/

static int from_status(int rc)
{
	switch (-rc) {
	case 0: rc = CYNARA_API_SUCCESS; break;
	case ENOMEM: rc = CYNARA_API_OUT_OF_MEMORY; break;
	case ENOTSUP: rc = CYNARA_API_METHOD_NOT_SUPPORTED; break;
	case ENOENT: rc = CYNARA_API_CACHE_MISS; break;
	default: rc = CYNARA_API_UNKNOWN_ERROR; break;
	}
	return rc;
}

static int from_check_status(int rc)
{
	switch (rc) {
	case 0: rc = CYNARA_API_ACCESS_DENIED; break;
	case 1: rc = CYNARA_API_ACCESS_ALLOWED; break;
	case -EEXIST: rc = CYNARA_API_ACCESS_NOT_RESOLVED; break;
	default: rc = from_status(rc); break;
	}
	return rc;
}

static int from_value(const char *value)
{
	if (!strcmp(value, "yes"))
		return CYNARA_ADMIN_ALLOW;
	if (!strcmp(value, "ask"))
		return CYNARA_ADMIN_ASK;
	return CYNARA_ADMIN_DENY;
}

static const char *to_value(int value)
{
	switch(value) {
	case CYNARA_ADMIN_DENY:
	case CYNARA_ADMIN_NONE:
	case CYNARA_ADMIN_BUCKET: return "no";
	case CYNARA_ADMIN_ALLOW: return "yes";
	case CYNARA_ADMIN_ASK: return "ask";
	}
	return "?";
}

/************************************ ERROR ****************************************/

static const struct {
	int num;
	const char *text;
} error_descriptions[] = {
	{ CYNARA_API_INTERRUPTED, "API call was interrupted by user" },
	{ CYNARA_API_ACCESS_NOT_RESOLVED, "access cannot be resolved without further actions" },
	{ CYNARA_API_ACCESS_ALLOWED, "access that was checked is allowed" },
	{ CYNARA_API_ACCESS_DENIED, "access that was checked is denied" },
	{ CYNARA_API_SUCCESS, "successful" },
	{ CYNARA_API_CACHE_MISS, "value is not present in cache" },
	{ CYNARA_API_MAX_PENDING_REQUESTS, "pending requests reached maximum" },
	{ CYNARA_API_OUT_OF_MEMORY, "system is running out of memory" },
	{ CYNARA_API_INVALID_PARAM, "parameter is malformed" },
	{ CYNARA_API_SERVICE_NOT_AVAILABLE, "service is not available" },
	{ CYNARA_API_METHOD_NOT_SUPPORTED, "method is not supported by library" },
	{ CYNARA_API_OPERATION_NOT_ALLOWED, "not allowed to perform requested operation" },
	{ CYNARA_API_OPERATION_FAILED, "failed to perform requested operation" },
	{ CYNARA_API_BUCKET_NOT_FOUND, "service hasn't found requested bucket" },
	{ CYNARA_API_UNKNOWN_ERROR, "unknown error" },
	{ CYNARA_API_CONFIGURATION_ERROR, "configuration error" },
	{ CYNARA_API_INVALID_COMMANDLINE_PARAM, "invalid parameter in command-line" },
	{ CYNARA_API_BUFFER_TOO_SHORT, "provided buffer is too short" },
	{ CYNARA_API_DATABASE_CORRUPTED, "database is corrupted" },
	{ CYNARA_API_PERMISSION_DENIED, "user doesn't have enough permission to perform action" },
};

int cynara_strerror(int errnum, char *buf, size_t buflen)
{
	int i = (int)(sizeof error_descriptions / sizeof *error_descriptions);
	while(i) {
		if (error_descriptions[--i].num == errnum) {
			if (strlen(error_descriptions[i].text) >= buflen)
				return CYNARA_API_BUFFER_TOO_SHORT;
			if (buf == NULL)
				break;
			strcpy(buf, error_descriptions[i].text);
			return CYNARA_API_SUCCESS;
		}
	}
	return CYNARA_API_INVALID_PARAM;
}

/******************** ADMIN ********************************/

struct cynara_admin;

int cynara_admin_initialize(struct cynara_admin **pp_cynara_admin)
{
	return from_status(rcyn_open((rcyn_t**)pp_cynara_admin, rcyn_Admin, 0));
}

int cynara_admin_finish(struct cynara_admin *p_cynara_admin)
{
	rcyn_close((rcyn_t*)p_cynara_admin);
	return CYNARA_API_SUCCESS;
}

int cynara_admin_set_policies(struct cynara_admin *p_cynara_admin,
                              const struct cynara_admin_policy *const *policies)
{
	int rc, rc2;
	const struct cynara_admin_policy *p;

	rc = rcyn_enter((rcyn_t*)p_cynara_admin);
	if (rc == 0) {
		p = *policies;
		while (rc == 0 && p != NULL) {
			if (p->result == CYNARA_ADMIN_DELETE)
				rc = rcyn_drop((rcyn_t*)p_cynara_admin,
						p->client, "*", p->user, p->privilege);
			else if (p->result != CYNARA_ADMIN_BUCKET && p->result != CYNARA_ADMIN_NONE)
				rc = rcyn_set((rcyn_t*)p_cynara_admin,
						p->client, "*", p->user, p->privilege, to_value(p->result), 0);
			p = *++policies;
		}
		rc2 = rcyn_leave((rcyn_t*)p_cynara_admin, rc == 0);
		if (rc == 0)
			rc = rc2;
	}
	return rc;
}

static void check_cb(
	void *closure,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char *value,
	time_t expire
) {
	*((int*)closure) = from_value(value);
}

int cynara_admin_check(struct cynara_admin *p_cynara_admin,
                       const char *start_bucket, const int recursive,
                       const char *client, const char *user, const char *privilege,
                       int *result, char **result_extra)
{
	if (result_extra)
		*result_extra = NULL;
	*result = CYNARA_ADMIN_DENY;
	return from_status(rcyn_get((rcyn_t*)p_cynara_admin, client, "*", user, privilege, check_cb, result));
}

struct list_data
{
	struct cynara_admin_policy **policies;
	const char *bucket;
	unsigned count;
	int error;
};

static void list_cb(
	void *closure,
	const char *client,
	const char *session,
	const char *user,
	const char *permission,
	const char *value,
	time_t expire
) {
	struct list_data *data = closure;
	struct cynara_admin_policy *pol;

	if (data->error)
		return;

	pol = calloc(1, sizeof *pol);
	if (pol == NULL)
		goto error;

	pol->bucket = strdup(data->bucket ?: "");
	pol->client = strdup(client);
	pol->user = strdup(user);
	pol->privilege = strdup(permission);
	if (pol->bucket == NULL || pol->client == NULL || pol->user == NULL || pol->privilege == NULL)
		goto error;

	pol->result = from_value(value);
	pol->result_extra = 0;
	closure = realloc(data->policies, (data->count + 1) * sizeof *data->policies);
	if (closure == NULL)
		goto error;

	(data->policies = closure)[data->count++] = pol;
	return;
error:
	if (pol) {
		free(pol->bucket);
		free(pol->client);
		free(pol->user);
		free(pol->privilege);
		free(pol);
	}
	data->error = -ENOMEM;

}

int cynara_admin_list_policies(struct cynara_admin *p_cynara_admin, const char *bucket,
                               const char *client, const char *user, const char *privilege,
                               struct cynara_admin_policy ***policies)
{
	int rc;
	struct list_data data;

	data.policies = NULL;
	data.bucket = bucket && strcmp(bucket, "#") && strcmp(bucket, "*") ? bucket : NULL;
	data.count = 0;
	data.error = 0;
	rc = rcyn_get((rcyn_t*)p_cynara_admin, client, "*", user, privilege, list_cb, &data);
	if (rc == 0 && data.error != 0)
		rc = data.error;
	if (rc == 0 && !data.error) {
		if ((*policies = realloc(data.policies, (data.count + 1) * sizeof *data.policies)) != NULL)
			policies[0][data.count] = NULL;
		else
			rc = -ENOMEM;
	}
	if (rc) {
		while(data.count)
			free(data.policies[--data.count]);
		free(data.policies);
		*policies = NULL;
	}
	return from_status(rc);
}

int cynara_admin_erase(struct cynara_admin *p_cynara_admin,
                       const char *start_bucket, int recursive,
                       const char *client, const char *user, const char *privilege)
{
	int rc, rc2;

	rc = rcyn_enter((rcyn_t*)p_cynara_admin);
	if (rc == 0) {
		rc = rcyn_drop((rcyn_t*)p_cynara_admin,
					client, "*", user, privilege);
		rc2 = rcyn_leave((rcyn_t*)p_cynara_admin, rc == 0);
		if (rc == 0)
			rc = rc2;
	}
	return from_status(rc);
}


int cynara_admin_list_policies_descriptions(struct cynara_admin *p_cynara_admin,
                                            struct cynara_admin_policy_descr ***descriptions)
{
	struct cynara_admin_policy_descr **d = malloc(4 * sizeof *d), *s;
	if (d) {
		d[0] = malloc(sizeof *s);
		d[1] = malloc(sizeof *s);
		d[2] = malloc(sizeof *s);
		d[3] = NULL;
		if (d[0] != NULL && d[1] != NULL && d[2] != NULL) {
			d[0]->name = strdup("Deny");
			d[1]->name = strdup("AskUser");
			d[2]->name = strdup("Allow");
			if (d[0]->name != NULL && d[1]->name != NULL && d[2]->name != NULL) {
				d[0]->result = CYNARA_ADMIN_DENY;
				d[1]->result = CYNARA_ADMIN_ASK;
				d[2]->result = CYNARA_ADMIN_ALLOW;
				*descriptions = d;
				return CYNARA_API_SUCCESS;
			}
			free(d[0]->name);
			free(d[1]->name);
			free(d[2]->name);
		}
		free(d[0]);
		free(d[1]);
		free(d[2]);
	}
	*descriptions = NULL;
	return CYNARA_API_OUT_OF_MEMORY;
}

/************************************* CLIENT-ASYNC **************************************/
struct cynara_async_configuration { uint32_t szcache; };

int cynara_async_configuration_create(cynara_async_configuration **pp_conf)
{
	*pp_conf = malloc(sizeof(cynara_async_configuration));
	if (*pp_conf == NULL)
		return CYNARA_API_OUT_OF_MEMORY;
	(*pp_conf)->szcache = 0;
	return CYNARA_API_SUCCESS;
}

void cynara_async_configuration_destroy(cynara_async_configuration *p_conf)
{
	free(p_conf);
}

int cynara_async_configuration_set_cache_size(cynara_async_configuration *p_conf,
                                              size_t cache_size)
{
	p_conf->szcache = cache_size > 1000000 ? 1000000 : (uint32_t)cache_size;
	return CYNARA_API_SUCCESS;
}

struct reqasync
{
	struct reqasync *next;
	cynara_async *cynasync;
	cynara_response_callback callback;
	void *user_response_data;
	cynara_check_id id;
	bool canceled;
};

struct cynara_async
{
	rcyn_t *rcyn;
	cynara_status_callback callback;
	void *user_status_data;
	struct reqasync *reqs;
	cynara_check_id ids;
};

static int async_control_cb(void *closure, int op, int fd, uint32_t events)
{
	cynara_async *p_cynara = closure;
	cynara_async_status s = (events & EPOLLOUT) ? CYNARA_STATUS_FOR_RW : CYNARA_STATUS_FOR_READ;
	switch(op) {
	case EPOLL_CTL_ADD:
		p_cynara->callback(-1, fd, s, p_cynara->user_status_data);
		break;
	case EPOLL_CTL_MOD:
		p_cynara->callback(fd, fd, s, p_cynara->user_status_data);
		break;
	case EPOLL_CTL_DEL:
		p_cynara->callback(fd, -1, 0, p_cynara->user_status_data);
		break;
	}
	return 0;
}

int cynara_async_initialize(cynara_async **pp_cynara, const cynara_async_configuration *p_conf,
                            cynara_status_callback callback, void *user_status_data)
{
	int ret;
	cynara_async *p_cynara;

	p_cynara = malloc(sizeof *p_cynara);
	if (p_cynara == NULL)
		ret = CYNARA_API_OUT_OF_MEMORY;
	else {
		ret = from_status(rcyn_open(&p_cynara->rcyn, rcyn_Check, p_conf ? p_conf->szcache : 0));
		if (ret != CYNARA_API_SUCCESS)
			free(p_cynara);
		else {
			p_cynara->callback = callback;
			p_cynara->user_status_data = user_status_data;
			p_cynara->reqs = NULL;
			p_cynara->ids = 0;
			rcyn_async_setup(p_cynara->rcyn, async_control_cb, p_cynara);
			*pp_cynara = p_cynara;
		}
	}
	return ret;
}

void cynara_async_finish(cynara_async *p_cynara)
{
	struct reqasync *req;

	for(req = p_cynara->reqs ; req ; req = req->next) {
		if (!req->canceled) {
			req->callback(req->id, CYNARA_CALL_CAUSE_FINISH, 0, req->user_response_data);
			req->canceled = true;
		}
	}

	rcyn_close(p_cynara->rcyn);

	while((req = p_cynara->reqs)) {
		p_cynara->reqs = req->next;
		free(req);
	}
	free(p_cynara);
}

int cynara_async_check_cache(cynara_async *p_cynara, const char *client, const char *client_session,
                             const char *user, const char *privilege)
{
	return from_check_status(rcyn_cache_check(p_cynara->rcyn, client, client_session,user, privilege));
}

static void reqcb(void *closure, int status)
{
	struct reqasync *req = closure, **p;

	p = &req->cynasync->reqs;
	while(*p && *p != req)
		p = &(*p)->next;
	if (*p)
		*p = req->next;

	if (!req->canceled)
		req->callback(req->id, CYNARA_CALL_CAUSE_ANSWER, from_check_status(status), req->user_response_data);

	free(req);
}

static int create_reqasync(cynara_async *p_cynara, const char *client,
                                const char *client_session, const char *user, const char *privilege,
                                cynara_check_id *p_check_id, cynara_response_callback callback,
                                void *user_response_data, bool simple)
{
	int rc;
	struct reqasync *req;

	req = malloc(sizeof *req);
	if (req == NULL)
		return CYNARA_API_OUT_OF_MEMORY;

	req->next = p_cynara->reqs;
	req->cynasync = p_cynara;
	req->callback = callback;
	req->user_response_data = user_response_data;
	req->id = ++p_cynara->ids;
	req->canceled = false;

	rc = rcyn_async_check(p_cynara->rcyn, client, client_session, user, privilege, simple, reqcb, req);
	if (rc == 0)
		p_cynara->reqs = req;
	else
		free(req);
	return from_status(rc);
}

int cynara_async_create_request(cynara_async *p_cynara, const char *client,
                                const char *client_session, const char *user, const char *privilege,
                                cynara_check_id *p_check_id, cynara_response_callback callback,
                                void *user_response_data)
{
	return create_reqasync(p_cynara, client, client_session, user, privilege, p_check_id, callback, user_response_data, false);
}

int cynara_async_create_simple_request(cynara_async *p_cynara, const char *client,
                                       const char *client_session, const char *user,
                                       const char *privilege, cynara_check_id *p_check_id,
                                       cynara_response_callback callback, void *user_response_data)
{
	return create_reqasync(p_cynara, client, client_session, user, privilege, p_check_id, callback, user_response_data, true);
}


int cynara_async_process(cynara_async *p_cynara)
{
	return rcyn_async_process(p_cynara->rcyn);
}

int cynara_async_cancel_request(cynara_async *p_cynara, cynara_check_id check_id)
{
	struct reqasync *req = p_cynara->reqs;

	while(req && req->id != check_id)
		req = req->next;
	if (req && !req->canceled) {
		req->canceled = true;
		req->callback(req->id, CYNARA_CALL_CAUSE_CANCEL, 0, req->user_response_data);
	}
	return CYNARA_API_SUCCESS;
}

/************************************* CLIENT **************************************/

struct cynara_configuration { uint32_t szcache; };

int cynara_configuration_create(cynara_configuration **pp_conf)
{
	*pp_conf = malloc(sizeof(cynara_configuration));
	if (*pp_conf == NULL)
		return CYNARA_API_OUT_OF_MEMORY;
	(*pp_conf)->szcache = 0;
	return CYNARA_API_SUCCESS;
}

void cynara_configuration_destroy(cynara_configuration *p_conf)
{
	free(p_conf);
}

int cynara_configuration_set_cache_size(cynara_configuration *p_conf,
                                              size_t cache_size)
{
	p_conf->szcache = cache_size > 1000000 ? 1000000 : (uint32_t)cache_size;
	return CYNARA_API_SUCCESS;
}

int cynara_initialize(cynara **pp_cynara, const cynara_configuration *p_conf)
{
	return from_status(rcyn_open((rcyn_t**)pp_cynara, rcyn_Check, p_conf ? p_conf->szcache : 0));
}

int cynara_finish(cynara *p_cynara)
{
	rcyn_close((rcyn_t*)p_cynara);
	return CYNARA_API_SUCCESS;
}

int cynara_check(cynara *p_cynara, const char *client, const char *client_session, const char *user,
                 const char *privilege)
{
	return from_check_status(rcyn_check((rcyn_t*)p_cynara, client, client_session, user, privilege));
}

int cynara_simple_check(cynara *p_cynara, const char *client, const char *client_session,
                        const char *user, const char *privilege)
{
	return from_check_status(rcyn_test((rcyn_t*)p_cynara, client, client_session, user, privilege));
}

/************************************* CREDS... & SESSION *********************************/
#define MAX_LABEL_LENGTH  1024

#if !defined(DEFAULT_PEERSEC_LABEL)
#  define DEFAULT_PEERSEC_LABEL "NoLabel"
#endif

int cynara_creds_get_default_client_method(enum cynara_client_creds *method)
{
	*method = CLIENT_METHOD_SMACK;
	return CYNARA_API_SUCCESS;
}

int cynara_creds_get_default_user_method(enum cynara_user_creds *method)
{
	*method = USER_METHOD_UID;
	return CYNARA_API_SUCCESS;
}

int cynara_creds_self_get_client(enum cynara_client_creds method, char **client)
{
	char label[MAX_LABEL_LENGTH + 1];
	int len, fd;

	label[0] = 0;
	fd = open("/proc/self/current/attr", O_RDONLY);
	if (fd >= 0) {
		len = (int)read(fd, label, sizeof label - 1);
		label[len >= 0 ? len : 0] = 0;
		close(fd);
	}
	return (*client = strdup(label[0] ? label : DEFAULT_PEERSEC_LABEL))
			? CYNARA_API_SUCCESS : CYNARA_API_OUT_OF_MEMORY;
}

int cynara_creds_self_get_user(enum cynara_user_creds method, char **user)
{
	return asprintf(user, "%ld", (long)getuid()) > 0
			? CYNARA_API_SUCCESS : CYNARA_API_OUT_OF_MEMORY;
}

int cynara_creds_socket_get_client(int fd, enum cynara_client_creds method, char **client)
{
        int rc;
        socklen_t length;
        struct ucred ucred;
        char label[MAX_LABEL_LENGTH + 1];

        /* get the credentials */
        length = (socklen_t)(sizeof ucred);
        rc = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &length);
        if (rc < 0 || length != (socklen_t)(sizeof ucred) || !~ucred.uid)
                return CYNARA_API_OPERATION_FAILED;

        /* get the security label */
        length = (socklen_t)(sizeof label);
        rc = getsockopt(fd, SOL_SOCKET, SO_PEERSEC, label, &length);
        if (rc < 0 || length > (socklen_t)(sizeof label))
                return CYNARA_API_OPERATION_FAILED;

	return (*client = strdup(label))
			? CYNARA_API_SUCCESS : CYNARA_API_OUT_OF_MEMORY;
}



int cynara_creds_socket_get_user(int fd, enum cynara_user_creds method, char **user)
{
        int rc;
        socklen_t length;
        struct ucred ucred;

        /* get the credentials */
        length = (socklen_t)(sizeof ucred);
        rc = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &length);
        if (rc < 0 || length != (socklen_t)(sizeof ucred) || !~ucred.uid)
                return CYNARA_API_OPERATION_FAILED;
	return asprintf(user, "%ld", (long)ucred.uid) > 0
			? CYNARA_API_SUCCESS : CYNARA_API_OUT_OF_MEMORY;
}



int cynara_creds_socket_get_pid(int fd, pid_t *pid)
{
        int rc;
        socklen_t length;
        struct ucred ucred;

        /* get the credentials */
        length = (socklen_t)(sizeof ucred);
        rc = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &ucred, &length);
        if (rc < 0 || length != (socklen_t)(sizeof ucred) || !~ucred.uid)
                return CYNARA_API_OPERATION_FAILED;
	*pid = ucred.pid;
	return CYNARA_API_SUCCESS;
}

char *cynara_session_from_pid(pid_t client_pid)
{
	char *r;

	return asprintf(&r, "%ld", (long)client_pid) < 0 ? NULL : r;
}

