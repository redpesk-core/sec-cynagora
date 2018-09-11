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

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/capability.h>

#if defined(WITH_SYSTEMD_ACTIVATION)
#include <systemd/sd-daemon.h>
#endif

#include "db.h"
#include "cyn.h"
#include "rcyn-server.h"

#if !defined(DEFAULT_DB_DIR)
#    define  DEFAULT_DB_DIR      "/var/lib/cynara"
#endif
#if !defined(DEFAULT_SOCKET_DIR)
#    define  DEFAULT_SOCKET_DIR  "/var/run/cynara"
#endif
#if !defined(DEFAULT_INIT_FILE)
#    define  DEFAULT_INIT_FILE   "/etc/security/cynara.initial"
#endif
#if !defined(DEFAULT_CYNARA_USER)
#    define  DEFAULT_CYNARA_USER   NULL
#endif
#if !defined(DEFAULT_CYNARA_GROUP)
#    define  DEFAULT_CYNARA_GROUP  NULL
#endif

#define _DBDIR_       'd'
#define _GROUP_       'g'
#define _HELP_        'h'
#define _INIT_        'i'
#define _MAKEDBDIR_   'm'
#define _MAKESOCKDIR_ 'M'
#define _OWNSOCKDIR_  'O'
#define _OWNDBDIR_    'o'
#define _SOCKETDIR_   'S'
#define _SYSTEMD_     's'
#define _USER_        'u'
#define _VERSION_     'v'

static
const char
shortopts[] = "d:g:hi:mMOoS:u:v"
#if defined(WITH_SYSTEMD_ACTIVATION)
	"s"
#endif
;

static
const struct option
longopts[] = {
	{ "dbdir", 1, NULL, _DBDIR_ },
	{ "group", 1, NULL, _GROUP_ },
	{ "help", 0, NULL, _HELP_ },
	{ "init", 1, NULL, _INIT_ },
	{ "make-db-dir", 0, NULL, _MAKEDBDIR_ },
	{ "make-socket-dir", 0, NULL, _MAKESOCKDIR_ },
	{ "own-db-dir", 0, NULL, _OWNDBDIR_ },
	{ "own-socket-dir", 0, NULL, _OWNSOCKDIR_ },
	{ "socketdir", 1, NULL, _SOCKETDIR_ },
#if defined(WITH_SYSTEMD_ACTIVATION)
	{ "systemd", 0, NULL, _SYSTEMD_ },
#endif
	{ "user", 1, NULL, _USER_ },
	{ "version", 0, NULL, _VERSION_ },
	{ NULL, 0, NULL, 0 }
};

static
const char
helptxt[] =
	"\n"
	"usage: cynarad [options]...\n"
	"\n"
	"otpions:\n"
#if defined(WITH_SYSTEMD_ACTIVATION)
	"       -s, --systemd         socket activation by systemd\n"
#endif
	"	-u, --user xxx        set the user\n"
	"	-g, --group xxx       set the group\n"
	"       -i, --init xxx        initialize if needed the database with content of file xxx\n"
	"\n"
	"	-b, --dbdir xxx       set the directory of database\n"
	"                              (default: "DEFAULT_DB_DIR")\n"
	"	-m, --make-db-dir     make the database directory\n"
	"	-o, --own-db-dir      set user and group on database directory\n"
	"\n"
	"	-S, --socketdir xxx   set the base xxx for sockets\n"
	"                              (default: "DEFAULT_SOCKET_DIR")\n"
	"	-M, --make-socket-dir make the socket directory\n"
	"	-O, --own-socket-dir  set user and group on socket directory\n"
	"\n"
	"       -h, --help            print this help and exit\n"
	"       -v, --version         print the version and exit\n"
	"\n"
;

static
const char
versiontxt[] =
	"cynarad version 1.99.99\n"
;

static int isid(const char *text);
static void ensure_directory(const char *path, int uid, int gid);
static void initdb(const char *path);
int main(int ac, char **av)
{
	int opt;
	int rc;
	int makesockdir = 0;
	int makedbdir = 0;
	int owndbdir = 0;
	int ownsockdir = 0;
	int help = 0;
	int version = 0;
	int error = 0;
	int systemd = 0;
	int uid = -1;
	int gid = -1;
	const char *init = NULL;
	const char *dbdir = NULL;
	const char *socketdir = NULL;
	const char *user = NULL;
	const char *group = NULL;
	struct passwd *pw;
	struct group *gr;
	cap_t caps = { 0 };
	rcyn_server_t *server;
	char *spec_socket_admin, *spec_socket_check;

	/* scan arguments */
	for (;;) {
		opt = getopt_long(ac, av, shortopts, longopts, NULL);
		if (opt == -1)
			break;

		switch(opt) {
		case _DBDIR_:
			dbdir = optarg;
			break;
		case _GROUP_:
			group = optarg;
			break;
		case _HELP_:
			help = 1;
			break;
		case _INIT_:
			init = optarg;
			break;
		case _MAKEDBDIR_:
			makedbdir = 1;
			break;
		case _MAKESOCKDIR_:
			makesockdir = 1;
			break;
		case _OWNSOCKDIR_:
			ownsockdir = 1;
			break;
		case _OWNDBDIR_:
			owndbdir = 1;
			break;
		case _SOCKETDIR_:
			socketdir = optarg;
			break;
#if defined(WITH_SYSTEMD_ACTIVATION)
		case _SYSTEMD_:
			systemd = 1;
			break;
#endif
		case _USER_:
			user = optarg;
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
	if (help | version) {
		fprintf(stdout, "%s", help ? helptxt : versiontxt);
		return 0;
	}
	if (error)
		return 1;
	if (systemd && (socketdir || makesockdir)) {
		fprintf(stderr, "can't set options --systemd and --%s together\n",
			socketdir ? "socketdir" : "make-socket-dir");
		return 1;
	}

	/* set the defaults */
	dbdir = dbdir ?: DEFAULT_DB_DIR;
	socketdir = socketdir ?: DEFAULT_SOCKET_DIR;
	user = user ?: DEFAULT_CYNARA_USER;
	group = group ?: DEFAULT_CYNARA_GROUP;
	init = init ?: DEFAULT_INIT_FILE;

	/* compute socket specs */
	spec_socket_admin = spec_socket_check = NULL;
	if (systemd) {
		spec_socket_admin = strdup("sd:admin");
		spec_socket_check = strdup("sd:check");
	} else {
		rc = asprintf(&spec_socket_admin, "unix:%s/cynara.admin", socketdir);
		rc = asprintf(&spec_socket_check, "unix:%s/cynara.check", socketdir);
	}
	if (spec_socket_admin == NULL || spec_socket_check == NULL) {
		fprintf(stderr, "can't make socket paths\n");
		return 1;
	}

	/* compute user and group */
	if (user) {
		uid = isid(user);
		if (uid < 0) {
			pw = getpwnam(user);
			if (pw == NULL) {
				fprintf(stderr, "can not find user '%s'\n", user);
				return -1;
			}
			uid = pw->pw_uid;
			gid = pw->pw_gid;
		}
	}
	if (group) {
		gid = isid(group);
		if (gid < 0) {
			gr = getgrnam(group);
			if (gr == NULL) {
				fprintf(stderr, "can not find group '%s'\n", group);
				return -1;
			}
			gid = gr->gr_gid;
		}
	}

	/* handle directories */
	if (makedbdir)
		ensure_directory(dbdir, owndbdir ? uid : -1, owndbdir ? gid : -1);
	if (makesockdir && socketdir[0] != '@')
		ensure_directory(socketdir, ownsockdir ? uid : -1, ownsockdir ? gid : -1);

	/* drop privileges */
	if (gid >= 0) {
		rc = setgid(gid);
		if (rc < 0) {
			fprintf(stderr, "can not change group: %m\n");
			return -1;
		}
	}
	if (uid >= 0) {
		rc = setuid(uid);
		if (rc < 0) {
			fprintf(stderr, "can not change user: %m\n");
			return -1;
		}
	}
	cap_clear(caps);
	rc = cap_set_proc(caps);

	/* connection to the database */
	rc = db_open(dbdir);
	if (rc < 0) {
		fprintf(stderr, "can not open database: %m\n");
		return 1;
	}

	/* initialisation of the database */
	if (db_is_empty()) {
		initdb(init);
		if (rc == 0)
			rc = db_sync();
		if (rc == 0)
			rc = db_backup();
		if (rc < 0) {
			fprintf(stderr, "can't initialise database: %m\n");
			return 1;
		}
	}

	/* initialize server */
	signal(SIGPIPE, SIG_IGN); /* avoid SIGPIPE! */
	rc = rcyn_server_create(&server, spec_socket_admin, spec_socket_check);
	if (rc < 0) {
		fprintf(stderr, "can't initialise server: %m\n");
		return 1;
	}

	/* ready ! */
#if defined(WITH_SYSTEMD_ACTIVATION)
	if (systemd)
		sd_notify(0, "READY=1");
#endif

	/* serve */
	rc = rcyn_server_serve(server);
	return rc ? 3 : 0;
}

/** returns the value of the id for 'text' (positive) or a negative value (-1) */
static int isid(const char *text)
{
	long long int value = 0;
	while(*text && value < INT_MAX)
		if (*text < '0' || *text > '9' || value >= INT_MAX)
			return -1;
		else
			value = 10 * value + (*text++ - '0');
	return value <= INT_MAX ? (int)value : -1;
}

/** returns a pointer to the first last / of the path if it is meaningful */
static char *enddir(char *path)
{
	/*
	 * /       -> NULL
	 * /xxx    -> NULL
	 * /xxx/   -> NULL
	 * /xxx/y  -> /y
	 * /xxx//y -> //y
	 */
	char *c = NULL, *r = NULL, *i = path;
	for(;;) {
		while(*i == '/')
			i++;
		if (*i)
			r = c;
		while(*i != '/')
			if (!*i++)
				return r;
		c = i;
	}
}

/** ensure that 'path' is a directory for the user and group */
static void ensuredir(char *path, int length, int uid, int gid)
{
	struct stat st;
	int rc, n;
	char *e;

	n = length;
	for(;;) {
		path[n] = 0;
		rc = mkdir(path, 0755);
		if (rc == 0 || errno == EEXIST) {
			/* exists */
			if (n == length) {
				rc = stat(path, &st);
				if (rc < 0) {
					fprintf(stderr, "can not check %s: %m\n", path);
					exit(1);
				} else if ((st.st_mode & S_IFMT) != S_IFDIR) {
					fprintf(stderr, "not a directory %s: %m\n", path);
					exit(1);
				}
				/* set ownership */
				if ((uid != st.st_uid && uid >= 0) || (gid != st.st_gid && gid >= 0)) {
					rc = chown(path, uid, gid);
					if (rc < 0) {
						fprintf(stderr, "can not own directory %s for uid=%d & gid=%d: %m\n", path, uid, gid);
						exit(1);
					}
				}
				return;
			}
			path[n] = '/';
			n = (int)strlen(path);
		} else if (errno == ENOENT) {
			/* a part of the path doesn't exist, try to create it */ 
			e = enddir(path);
			if (!e) {
				/* can't create it because at root */
				fprintf(stderr, "can not ensure directory %s\n", path);
				exit(1);
			}
			n = (int)(e - path);
		} else {
			fprintf(stderr, "can not ensure directory %s: %m\n", path);
			exit(1);
		}
	}
}

/** ensure that 'path' is a directory for the user and group */
static void ensure_directory(const char *path, int uid, int gid)
{
	size_t l;
	char *p;

	l = strlen(path);
	if (l > INT_MAX) {
		/* ?!?!?!? *#@! */
		fprintf(stderr, "path toooooo long\n");
		exit(1);
	}
	p = strndupa(path, l);
	ensuredir(p, (int)l, uid, gid);
}

/** initialize the database from file of 'path' */
static void initdb(const char *path)
{
	int rc, lino, x;
	char *item[10];
	char buffer[2048];
	FILE *f;

	f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "can't open file %s\n", path);
		exit(1);
	}

	lino = 0;
	while(fgets(buffer, sizeof buffer, f)) {
		lino++;
		item[0] = strtok(buffer, " \t\n\r");
		if (item[0] && item[0][0] != '#') {
			item[1] = strtok(NULL, " \t\n\r");
			item[2] = strtok(NULL, " \t\n\r");
			item[3] = strtok(NULL, " \t\n\r");
			item[4] = strtok(NULL, " \t\n\r");
			item[5] = strtok(NULL, " \t\n\r");
			if (item[1] == NULL || item[2] == NULL
			  || item[3] == NULL || item[4] == NULL) {
				fprintf(stderr, "field missing (%s:%d)\n", path, lino);
				exit(1);
			} else if (item[5] != NULL && item[5][0] != '#') {
				fprintf(stderr, "extra field (%s:%d)\n", path, lino);
				exit(1);
			}
			x = isid(item[4]);
			if (x < 0) {
				fprintf(stderr, "bad value (%s:%d)\n", path, lino);
				exit(1);
			}
			rc = db_set(item[0], item[1], item[2], item[3], x);
			if (rc < 0) {
				fprintf(stderr, "can't set (%s:%d)\n", path, lino);
				exit(1);
			}
		}
	}
	if (!feof(f)) {
		fprintf(stderr, "error while reading file %s\n", path);
		exit(1);
	}
	fclose(f);
}

