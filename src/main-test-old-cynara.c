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

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>

#include <cynara/cynara-admin.h>
#include <cynara/cynara-client-async.h>
#include <cynara/cynara-client.h>

#define STD 0
#define TEST 1
#define CACHE 2

#define STRFY(x) #x
#define CKEX(x) ckex((x),STD,__LINE__,STRFY(x))
#define TEEX(x) ckex((x),TEST,__LINE__,STRFY(x))
#define CAEX(x) ckex((x),CACHE,__LINE__,STRFY(x))

struct cynara_admin *admin;
struct cynara_async_configuration *aconf;
struct cynara_async *aclient;
struct cynara_configuration *conf;
struct cynara *client;
char buffer[4000];
char *str[40];
int nstr;
int pollfd;

#define BUCKET "BUCK"

void ckex(int rc, int type, int line, const char *x)
{
	int err = 1;
	switch(type) {
	case STD:
		err = (rc != CYNARA_API_SUCCESS);
		break;
	case TEST:
		err = (rc != CYNARA_API_ACCESS_DENIED
			 && rc != CYNARA_API_ACCESS_ALLOWED
			 && rc != CYNARA_API_ACCESS_NOT_RESOLVED);
		break;
	case CACHE:
		err = (rc != CYNARA_API_ACCESS_DENIED
			 && rc != CYNARA_API_ACCESS_ALLOWED
			 && rc != CYNARA_API_ACCESS_NOT_RESOLVED
			 && rc != CYNARA_API_CACHE_MISS);
		break;
	}
	if (err) {
		char buffer[200];
		cynara_strerror(rc, buffer, 200);
		printf("ERROR(%d) %s by %s line %d\n", rc, buffer, x, line);
		exit(1);
	}
	printf("SUCCESS[%d] %s\n", rc, x);
}

int is(const char *first, const char *second, int mincount)
{
	return nstr >= mincount + 2
		&& !strcmp(first, str[0])
		&& !strcmp(second, str[1]);
}

void adm_list(char *cli, char *usr, char *perm)
{
	int i;
	struct cynara_admin_policy **policies;
	CKEX(cynara_admin_list_policies(admin, BUCKET, cli, usr, perm, &policies));
	i = 0;
	while(policies[i]) {
		printf("%s %s %s %s %d %s\n",
			policies[i]->bucket,
			policies[i]->client,
			policies[i]->user,
			policies[i]->privilege,
			policies[i]->result,
			policies[i]->result_extra ?: "");
		free(policies[i]->bucket);
		free(policies[i]->client);
		free(policies[i]->user);
		free(policies[i]->privilege);
		free(policies[i]->result_extra);
		free(policies[i]);
		i++;
	}
	free(policies);
}

void adm_check(char *cli, char *usr, char *perm)
{
	char *rs;
	int ri;

	CKEX(cynara_admin_check(admin, BUCKET, 1, cli, usr, perm, &ri, &rs));
	printf("got %d %s \n", ri, rs ?: "NULL");
}

void adm_set()
{
	struct cynara_admin_policy **policies, *p;
	int n, i;

	n = (nstr - 2) / 4;
	policies = malloc((1 + n) * sizeof *policies + n * sizeof **policies);
	policies[n] = NULL;
	p = (struct cynara_admin_policy*)(&policies[n + 1]);
	for (i = 0 ; i < n ; i++, p++) {
		policies[i] = p;
		p->bucket = BUCKET;
		p->client = str[2 + i * 4 + 0];
		p->user = str[2 + i * 4 + 1];
		p->privilege = str[2 + i * 4 + 2];
		p->result = atoi(str[2 + i * 4 + 3]);
		p->result_extra = NULL;
	}
	CKEX(cynara_admin_set_policies(admin, (const struct cynara_admin_policy *const *)policies));
	free(policies);
}

void adm_erase(char *cli, char *usr, char *perm)
{
	CKEX(cynara_admin_erase(admin, BUCKET, 1, cli, usr, perm));
}

void adm_desc()
{
	int i;
	struct cynara_admin_policy_descr **d;
	CKEX(cynara_admin_list_policies_descriptions(admin, &d));
	i = 0;
	while(d[i]) {
		printf("desc[%d] %d -> %s\n", i, d[i]->result, d[i]->name);
		free(d[i]->name);
		free(d[i]);
		i++;
	}
	free(d);
}

void asy_cache(char *cli, char *ses, char *usr, char *perm)
{
	CAEX(cynara_async_check_cache(aclient, cli, ses, usr, perm));
}

void asyncb(cynara_check_id check_id, cynara_async_call_cause cause,
           int response, void *user_response_data)
{
	printf("RECEIVE %d %d\n", cause, response);
}

void asy_check(char *cli, char *ses, char *usr, char *perm, int simple)
{
	if (simple)
		CKEX(cynara_async_create_simple_request(aclient, cli, ses, usr, perm, NULL, asyncb, NULL));
	else
		CKEX(cynara_async_create_request(aclient, cli, ses, usr, perm, NULL, asyncb, NULL));
}

void syn_check(char *cli, char *ses, char *usr, char *perm, int simple)
{
	if (simple)
		TEEX(cynara_simple_check(client, cli, ses, usr, perm));
	else
		TEEX(cynara_check(client, cli, ses, usr, perm));
}

void asyncstscb(int old_fd, int new_fd, cynara_async_status status, void *data)
{
	struct epoll_event ev;

	ev.data.fd = new_fd;
	ev.events = (status == CYNARA_STATUS_FOR_RW ? EPOLLOUT : 0)|EPOLLIN;
	if (old_fd == new_fd) {
		if (new_fd != -1)
			epoll_ctl(pollfd, EPOLL_CTL_MOD, new_fd, &ev);
	} else {
		if (old_fd != -1)
			epoll_ctl(pollfd, EPOLL_CTL_DEL, old_fd, &ev);
		if (new_fd != -1)
			epoll_ctl(pollfd, EPOLL_CTL_ADD, new_fd, &ev);
	}
}

int main(int ac, char **av)
{
	struct epoll_event ev;

	pollfd = epoll_create(10);
	ev.data.fd = 0;
	ev.events = EPOLLIN;
	epoll_ctl(pollfd, EPOLL_CTL_ADD, 0, &ev);

	CKEX(cynara_admin_initialize(&admin));

	CKEX(cynara_async_configuration_create(&aconf));
	CKEX(cynara_async_configuration_set_cache_size(aconf, 1000));
	CKEX(cynara_async_initialize(&aclient, aconf, asyncstscb, NULL));
	cynara_async_configuration_destroy(aconf);

	CKEX(cynara_configuration_create(&conf));
	CKEX(cynara_configuration_set_cache_size(conf, 1000));
	CKEX(cynara_initialize(&client, conf));
	cynara_configuration_destroy(conf);

	for(;;) {
		epoll_wait(pollfd, &ev, 1, -1);

		if (ev.data.fd) {
			cynara_async_process(aclient);
			continue;
		}

		if (!fgets(buffer, sizeof buffer, stdin))
			break;

		str[nstr = 0] = strtok(buffer, " \t\n");
		while(str[nstr])
			str[++nstr] = strtok(NULL, " \t\n");

		if (is("admin", "listall", 0))
			adm_list("#", "#", "#");
		else if (is("admin", "list", 3))
			adm_list(str[2], str[3], str[4]);
		else if (is("admin", "check", 3))
			adm_check(str[2], str[3], str[4]);
		else if (is("admin", "set", 4))
			adm_set();
		else if (is("admin", "erase", 3))
			adm_erase(str[2], str[3], str[4]);
		else if (is("admin", "desc", 0))
			adm_desc();
		else if (is("async", "cache", 4))
			asy_cache(str[2], str[3], str[4], str[5]);
		else if (is("async", "check", 4))
			asy_check(str[2], str[3], str[4], str[5], 0);
		else if (is("async", "test", 4))
			asy_check(str[2], str[3], str[4], str[5], 1);
		else if (is("sync", "check", 4))
			syn_check(str[2], str[3], str[4], str[5], 0);
		else if (is("sync", "test", 4))
			syn_check(str[2], str[3], str[4], str[5], 1);
		else if (nstr > 0 && !strcmp(str[0], "exit"))
			break;
		else if (nstr > 0 && str[0][0] != '#')
			printf("ERROR bad input\n");
	}

	cynara_finish(client);
	cynara_async_finish(aclient);
	cynara_admin_finish(admin);
}

