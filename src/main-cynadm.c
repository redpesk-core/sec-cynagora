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
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include "rcyn-client.h"
#include "rcyn-protocol.h"
#include "expire.h"

#define _HELP_        'h'
#define _SOCKET_      's'
#define _VERSION_     'v'

static
const char
shortopts[] = "hs:v";

static
const struct option
longopts[] = {
	{ "help", 0, NULL, _HELP_ },
	{ "socket", 1, NULL, _SOCKET_ },
	{ "version", 0, NULL, _VERSION_ },
	{ NULL, 0, NULL, 0 }
};

static
const char
helptxt[] =
	"\n"
	"usage: cynadm [options]... [action [arguments]]\n"
	"\n"
	"otpions:\n"
	"	-s, --socket xxx      set the base xxx for sockets\n"
	"	                        (default: %s)\n"
	"	-h, --help            print this help and exit\n"
	"	-v, --version         print the version and exit\n"
	"\n"
	"When action is given, cynadm performs the action and exits.\n"
	"Otherwise cynadm continuously read its input to get the actions.\n"
	"For a list of actions tpe 'cynadm help'.\n"
	"\n"
;

static
const char
versiontxt[] =
	"cynadm version 1.99.99\n"
;

static
const char
help__text[] =
	"\n"
	"Commands are: list, set, drop, check, test, cache, clear, quit, help\n"
	"Type 'help command' to get help on the command\n"
	"Type 'help expiration' to get help on expirations\n"
	"\n"
;

static
const char
help_list_text[] =
	"\n"
	"Command: list [client [session [user [permission]]]]\n"
	"\n"
	"List the rules matching the optionally given 'client', 'session',\n"
	"'user', 'permission'.\n"
	"\n"
	"This command requires to be connected to the administrator socket.\n"
	"\n"
	"The value given can be '#' (sharp) to match any value. When no value\n"
	"is given, it is implied as being '#'.\n"
	"\n"
	"Examples:\n"
	"\n"
	"  list                         list all rules\n"
	"  list # # 1001                list the rules of the user 1001\n"
	"\n"
;

static
const char
help_set_text[] =
	"\n"
	"Command: set client session user permission value expiration\n"
	"\n"
	"Set the rule associating the given 'client', 'session', 'user'\n"
	"permission' with the 'value' for a time given by 'expiration'.\n"
	"\n"
	"Type 'help expiration' to get help on expirations\n"
	"\n"
	"This command requires to be connected to the administrator socket.\n"
	"\n"
	"Examples:\n"
	"\n"
	"  set * * 0 * yes *            set forever the value yes for user 0 and any\n"
	"                               permission, client or session.\n"
	"\n"
	"  set wrt * * X no 1d          set for one day the value no for client xrt and\n"
	"                               permission X of any user and session.\n"
	"\n"
;

static
const char
help_drop_text[] =
	"\n"
	"Command: drop [client [session [user [permission]]]]\n"
	"\n"
	"Removes the rules matching the optionally given 'client', 'session',\n"
	"'user', 'permission'.\n"
	"\n"
	"This command requires to be connected to the administrator socket.\n"
	"\n"
	"The value given can be '#' (sharp) to match any value. When no value\n"
	"is given, it is implied as being '#'.\n"
	"\n"
	"Examples:\n"
	"\n"
	"  drop                         drop all rules\n"
	"  drop # # 1001                drop the rules of the user 1001\n"
	"\n"
;

static
const char
help_check_text[] =
	"\n"
	"Command: check client session user permission\n"
	"\n"
	"Check authorisation for the given 'client', 'session', 'user', 'permission'.\n"
	"\n"
	"Examples:\n"
	"\n"
	"  check wrt W3llcomE 1001 audio        check that client 'wrt' of session\n"
	"                                       'W3llcomE' for user '1001' has the\n"
	"                                       'audio' permission\n"
	"\n"
;

static
const char
help_test_text[] =
	"\n"
	"Command: test client session user permission\n"
	"\n"
	"Test authorisation for the given 'client', 'session', 'user', 'permission'.\n"
	"Same as command 'check' except that it doesn't use query agent if it were\n"
	"needed to avoid asynchronous timely unlimited queries.\n"
	"\n"
	"Examples:\n"
	"\n"
	"  test wrt W3llcomE 1001 audio        check that client 'wrt' of session\n"
	"                                       'W3llcomE' for user '1001' has the\n"
	"                                       'audio' permission\n"
	"\n"
;

static
const char
help_cache_text[] =
	"\n"
	"Command: cache client session user permission\n"
	"\n"
	"Test cache for authorisation for the given 'client', 'session', 'user', 'permission'.\n"
	"\n"
	"Examples:\n"
	"\n"
	"  cache wrt W3llcomE 1001 audio        check that client 'wrt' of session\n"
	"                                       'W3llcomE' for user '1001' has the\n"
	"                                       'audio' permission\n"
	"\n"
	"\n"
	"\n"
	"\n"
	"\n"
	"\n"
	"\n"
;

static
const char
help_clear_text[] =
	"\n"
	"Command: clear\n"
	"\n"
	"Clear the current cache.\n"
	"\n"
;

static
const char
help_quit_text[] =
	"\n"
	"Command: quit\n"
	"\n"
	"Quit the program\n"
	"\n"
;

static
const char
help_help_text[] =
	"\n"
	"Command: help [command | topic]\n"
	"\n"
	"Gives help on the command or on the topic.\n"
	"\n"
	"Available commands: list, set, drop, check, test, cache, clear, quit, help\n"
	"Available topics: expiration\n"
	"\n"
;

static
const char
help_expiration_text[] =
	"\n"
	"Expirations limited in the time are expressed using the scheme NyNdNhNmNs\n"
	"where N are numeric values and ydhms are unit specifications.\n"
	"Almost all part of the scheme are optional. The default unit is second.\n"
	"\n"
	"Unlimited expirations can be expressed using: 0, *, always or forever.\n"
	"\n"
	"Examples:\n"
	"\n"
	"  6y5d                         6 years and 5 days\n"
	"  1d6h                         1 day and 6 hours\n"
	"  56                           56 seconds\n"
	"  forever                      unlimited, no expiration\n"
	"\n"
;

static rcyn_t *rcyn;
static char buffer[4000];
static char *str[40];
static int nstr;

rcyn_key_t key;
rcyn_value_t value;

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

	key.client = n > 1 ? av[1] : def;
	key.session = n > 2 ? av[2] : def;
	key.user = n > 3 ? av[3] : def;
	key.permission = n > 4 ? av[4] : def;
	value.value = n > 5 ? av[5] : "no";
	value.expire = n > 6 ? txt2exp(av[6]) : 0;

	return key.client && key.session && key.user && key.permission && value.value && value.expire >= 0 ? 0 : -EINVAL;
}

int get_csup(int ac, char **av, int *used, const char *def)
{
	int n = plink(ac, av, used, 5);

	key.client = n > 1 ? av[1] : def;
	key.session = n > 2 ? av[2] : def;
	key.user = n > 3 ? av[3] : def;
	key.permission = n > 4 ? av[4] : def;

	return key.client && key.session && key.user && key.permission ? 0 : -EINVAL;
}

void listcb(void *closure, const rcyn_key_t *k, const rcyn_value_t *v)
{
	char buffer[100];
	int *p = closure;
	exp2txt(v->expire, buffer, sizeof buffer);
	fprintf(stdout, "%s\t%s\t%s\t%s\t%s\t%s\n", k->client, k->session, k->user, k->permission, v->value, buffer);
	(*p)++;
}

int do_list(int ac, char **av)
{
	int count, uc, rc;

	rc = get_csup(ac, av, &uc, "#");
	if (rc == 0) {
		count = 0;
		rc = rcyn_get(rcyn, &key, listcb, &count);
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
		rc = rcyn_set(rcyn, &key, &value);
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
		rc = rcyn_drop(rcyn, &key);
		rcyn_leave(rcyn, !rc);
	}
	if (rc < 0)
		fprintf(stderr, "error %s\n", strerror(-rc));
	return uc;
}

int do_check(int ac, char **av, int (*f)(rcyn_t*,const rcyn_key_t*))
{
	int uc, rc;

	rc = get_csup(ac, av, &uc, NULL);
	if (rc == 0) {
		rc = f(rcyn, &key);
		if (rc > 0)
			fprintf(stdout, "allowed\n");
		else if (rc == 0)
			fprintf(stdout, "denied\n");
		else if (rc == -ENOENT && f == rcyn_cache_check)
			fprintf(stdout, "not in cache!\n");
		else
			fprintf(stderr, "error %s\n", strerror(-rc));
	}
	return uc;
}

int do_help(int ac, char **av)
{
	if (ac > 1 && !strcmp(av[1], "list"))
		fprintf(stdout, "%s", help_list_text);
	else if (ac > 1 && !strcmp(av[1], "set"))
		fprintf(stdout, "%s", help_set_text);
	else if (ac > 1 && !strcmp(av[1], "drop"))
		fprintf(stdout, "%s", help_drop_text);
	else if (ac > 1 && !strcmp(av[1], "check"))
		fprintf(stdout, "%s", help_check_text);
	else if (ac > 1 && !strcmp(av[1], "test"))
		fprintf(stdout, "%s", help_test_text);
	else if (ac > 1 && !strcmp(av[1], "cache"))
		fprintf(stdout, "%s", help_cache_text);
	else if (ac > 1 && !strcmp(av[1], "clear"))
		fprintf(stdout, "%s", help_clear_text);
	else if (ac > 1 && !strcmp(av[1], "quit"))
		fprintf(stdout, "%s", help_quit_text);
	else if (ac > 1 && !strcmp(av[1], "help"))
		fprintf(stdout, "%s", help_help_text);
	else if (ac > 1 && !strcmp(av[1], "expiration"))
		fprintf(stdout, "%s", help_expiration_text);
	else {
		fprintf(stdout, "%s", help__text);
		return 1;
	}
	return 2;
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

	if (!strcmp(av[0], "quit"))
		return 0;

	if (!strcmp(av[0], "help") || !strcmp(av[0], "?"))
		return do_help(ac, av);

	fprintf(stderr, "unknown command %s\n", av[0]);
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
	int opt;
	int rc;
	int help = 0;
	int version = 0;
	int error = 0;
	const char *socket = NULL;

	/* scan arguments */
	for (;;) {
		opt = getopt_long(ac, av, shortopts, longopts, NULL);
		if (opt == -1)
			break;

		switch(opt) {
		case _HELP_:
			help = 1;
			break;
		case _SOCKET_:
			socket = optarg;
			break;
		case _VERSION_:
			version = 1;
			break;
		default:
			error = 1;
			break;
		}
	}

	/* handles help, version, error */
	if (help) {
		fprintf(stdout, helptxt, rcyn_default_admin_socket_spec);
		return 0;
	}
	if (version) {
		fprintf(stdout, versiontxt);
		return 0;
	}
	if (error)
		return 1;

	/* initialize server */
	signal(SIGPIPE, SIG_IGN); /* avoid SIGPIPE! */
	rc = rcyn_open(&rcyn, rcyn_Admin, 5000, socket);
	if (rc < 0) {
		fprintf(stderr, "initialisation failed: %s\n", strerror(-rc));
		return 1;
	}

	if (optind < ac) {
		do_all(ac - optind, av + optind);
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
