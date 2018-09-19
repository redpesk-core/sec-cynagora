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
#include <errno.h>
#include <time.h>
#include <signal.h>

#include "rcyn-client.h"

rcyn_t *rcyn;

char buffer[4000];
char *str[40];
int nstr;

static const int MIN = 60;
static const int HOUR = 60*MIN;
static const int DAY = 24*HOUR;
static const int YEAR = 365*DAY;

const char *client, *session, *user, *permission, *value;
time_t expire;
int txt2experr;

time_t txt2exp(const char *txt)
{
	time_t r, x;

	txt2experr = 0;
	if (!strcmp(txt, "always"))
		return 0;

	r = time(NULL);
	while(*txt) {
		x = 0;
		while('0' <= *txt && *txt <= '9')
			x = 10 * x + (time_t)(*txt++ - '0');
		switch(*txt) {
		case 'y': r += x * YEAR; txt++; break;
		case 'd': r += x *= DAY; txt++; break;
		case 'h': r += x *= HOUR; txt++; break;
		case 'm': r += x *= MIN; txt++; break;
		case 's': txt++; /*@fallthrough@*/
		case 0: r += x; break;
		default: txt2experr = 1; return -1;
		}
	}
	return r;
}

const char *exp2txt(time_t expire)
{
	static char buffer[200];
	int n;

	if (!expire)
		return "always";
	expire -= time(NULL);
	n = 0;
	if (expire >= YEAR) {
		n += snprintf(&buffer[n], sizeof buffer - n, "%lldy",
			(long long)(expire / YEAR));
		expire = expire % YEAR;
	}
	if (expire >= DAY) {
		n += snprintf(&buffer[n], sizeof buffer - n, "%lldd",
			(long long)(expire / DAY));
		expire = expire % DAY;
	}
	if (expire >= HOUR) {
		n += snprintf(&buffer[n], sizeof buffer - n, "%lldh",
			(long long)(expire / HOUR));
		expire = expire % HOUR;
	}
	if (expire >= MIN) {
		n += snprintf(&buffer[n], sizeof buffer - n, "%lldm",
			(long long)(expire / MIN));
		expire = expire % MIN;
	}
	if (expire) {
		n += snprintf(&buffer[n], sizeof buffer - n, "%llds",
			(long long)expire);
	}
	return buffer;
}

int plink(int ac, char **av, int *used, int maxi)
{
	int r = 0;

	if (maxi < ac)
		ac = maxi;
	while (r < ac && strcmp(av[r], ";"))
		r++;

	*used = r + (r < ac);
	return r;
}

int get_csupve(int ac, char **av, int *used, const char *def)
{
	int n = plink(ac, av, used, 7);

	client = n > 1 ? av[1] : def;
	session = n > 2 ? av[2] : def;
	user = n > 3 ? av[3] : def;
	permission = n > 4 ? av[4] : def;
	value = n > 5 ? av[5] : "no";
	expire = n > 6 ? txt2exp(av[6]) : 0;

	return client && session && user && permission && value && !txt2experr ? 0 : -EINVAL;
}

int get_csup(int ac, char **av, int *used, const char *def)
{
	int n = plink(ac, av, used, 5);

	client = n > 1 ? av[1] : def;
	session = n > 2 ? av[2] : def;
	user = n > 3 ? av[3] : def;
	permission = n > 4 ? av[4] : def;

	return client && session && user && permission ? 0 : -EINVAL;
}

void listcb(void *closure, const char *client, const char *session,
		const char *user, const char *permission,
		const char *value, time_t expire)
{
	int *p = closure;
	const char *e = exp2txt(expire);
	fprintf(stdout, "%s\t%s\t%s\t%s\t%s\t%s\n", client, session, user, permission, value, e);
	(*p)++;
}

int do_list(int ac, char **av)
{
	int count, uc, rc;

	rc = get_csup(ac, av, &uc, "#");
	if (rc == 0) {
		count = 0;
		rc = rcyn_get(rcyn, client, session, user, permission, listcb, &count);
		if (rc < 0)
			fprintf(stderr, "error %s\n", strerror(-rc));
		else
			fprintf(stdout, "%d entries found\n", count);
	}
	return uc;
}

int do_set(int ac, char **av)
{
	int uc, rc;

	rc = get_csupve(ac, av, &uc, "*");
	if (rc == 0)
		rc = rcyn_enter(rcyn);
	if (rc == 0) {
		rc = rcyn_set(rcyn, client, session, user, permission, value, expire);
		rcyn_leave(rcyn, !rc);
	}
	if (rc < 0)
		fprintf(stderr, "error %s\n", strerror(-rc));
	return uc;
}

int do_drop(int ac, char **av)
{
	int uc, rc;

	rc = get_csup(ac, av, &uc, "#");
	if (rc == 0)
		rc = rcyn_enter(rcyn);
	if (rc == 0) {
		rc = rcyn_drop(rcyn, client, session, user, permission);
		rcyn_leave(rcyn, !rc);
	}
	if (rc < 0)
		fprintf(stderr, "error %s\n", strerror(-rc));
	return uc;
}

int do_check(int ac, char **av, int (*f)(rcyn_t*,const char*,const char*,const char*,const char*))
{
	int uc, rc;

	rc = get_csup(ac, av, &uc, NULL);
	if (rc == 0) {
		rc = f(rcyn, client, session, user, permission);
		if (rc > 0)
			fprintf(stdout, "allowed\n");
		else if (rc == 0)
			fprintf(stdout, "denied\n");
		else
			fprintf(stderr, "error %s\n", strerror(-rc));
	}
	return uc;
}

int do_any(int ac, char **av)
{
	if (!ac)
		return 0;

	if (!strcmp(av[0], "list"))
		return do_list(ac, av);

	if (!strcmp(av[0], "set"))
		return do_set(ac, av);

	if (!strcmp(av[0], "drop"))
		return do_drop(ac, av);

	if (!strcmp(av[0], "check"))
		return do_check(ac, av, rcyn_check);

	if (!strcmp(av[0], "test"))
		return do_check(ac, av, rcyn_test);

	if (!strcmp(av[0], "cache"))
		return do_check(ac, av, rcyn_cache_check);

	if (!strcmp(av[0], "clear")) {
		rcyn_cache_clear(rcyn);
		return 1;
	}

	return 0;
}

void do_all(int ac, char **av)
{
	int rc;

	while(ac) {
		rc = do_any(ac, av);
		if (rc <= 0)
			exit(1);
		ac -= rc;
		av += rc;
	}
}

int main(int ac, char **av)
{
	int rc;

	signal(SIGPIPE, SIG_IGN); /* avoid SIGPIPE! */
	rc = rcyn_open(&rcyn, rcyn_Admin, 5000);
	if (rc < 0) {
		fprintf(stderr, "initialisation failed: %s\n", strerror(-rc));
		return 1;
	}
	if (ac > 1) {
		do_all(ac - 1, av + 1);
		return 0;
	}

	for(;;) {
		if (!fgets(buffer, sizeof buffer, stdin))
			break;

		str[nstr = 0] = strtok(buffer, " \t\n");
		while(str[nstr])
			str[++nstr] = strtok(NULL, " \t\n");

		ac = 0;
		while(ac < nstr) {
			rc = do_any(nstr - ac, &str[ac]);
			if (rc <= 0)
				exit(1);
			ac += rc;
		}
	}
	return 0;
}

