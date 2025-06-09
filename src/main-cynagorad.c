/*
 * Copyright (C) 2018-2025 IoT.bzh Company
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
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF CYNAGORA SERVER                                          */
/******************************************************************************/
/******************************************************************************/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/capability.h>

#if defined(WITH_SYSTEMD)
#include <systemd/sd-daemon.h>
#endif

#include "data.h"
#include "db.h"
#include "cyn.h"
#include "cyn-server.h"
#include "cyn-protocol.h"
#include "db-import.h"
#include "agent-at.h"

#if !defined(DEFAULT_DB_DIR)
#    define  DEFAULT_DB_DIR      "/var/lib/cynagora"
#endif
#if !defined(DEFAULT_INIT_FILE)
#    define  DEFAULT_INIT_FILE   "/etc/security/cynagora.initial"
#endif
#if !defined(DEFAULT_CYNAGORA_USER)
#    define  DEFAULT_CYNAGORA_USER   NULL
#endif
#if !defined(DEFAULT_CYNAGORA_GROUP)
#    define  DEFAULT_CYNAGORA_GROUP  NULL
#endif
#if !defined(DEFAULT_LOCKFILE)
#    define  DEFAULT_LOCKFILE      ".cynagora-lock"
#endif

#define _DBDIR_       'd'
#define _GROUP_       'g'
#define _HELP_        'h'
#define _INIT_        'i'
#define _LOG_         'l'
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
shortopts[] = "d:g:hi:lmMOoS:u:v";

static
const struct option
longopts[] = {
	{ "dbdir", 1, NULL, _DBDIR_ },
	{ "group", 1, NULL, _GROUP_ },
	{ "help", 0, NULL, _HELP_ },
	{ "init", 1, NULL, _INIT_ },
	{ "log", 0, NULL, _LOG_ },
	{ "make-db-dir", 0, NULL, _MAKEDBDIR_ },
	{ "make-socket-dir", 0, NULL, _MAKESOCKDIR_ },
	{ "own-db-dir", 0, NULL, _OWNDBDIR_ },
	{ "own-socket-dir", 0, NULL, _OWNSOCKDIR_ },
	{ "socketdir", 1, NULL, _SOCKETDIR_ },
	{ "user", 1, NULL, _USER_ },
	{ "version", 0, NULL, _VERSION_ },
	{ NULL, 0, NULL, 0 }
};

static
const char
helptxt[] =
	"\n"
	"usage: cynagorad [options]...\n"
	"\n"
	"options:\n"
	"	-u, --user xxx        set the user\n"
	"	-g, --group xxx       set the group\n"
	"	-i, --init xxx        initialize if needed the database with file xxx\n"
	"	                        (default: "DEFAULT_INIT_FILE"\n"
	"	-l, --log             activate log of transactions\n"
	"	-d, --dbdir xxx       set the directory of database\n"
	"	                        (default: "DEFAULT_DB_DIR")\n"
	"	-m, --make-db-dir     make the database directory\n"
	"	-o, --own-db-dir      set user and group on database directory\n"
	"\n"
	"	-S, --socketdir xxx   set the base directory xxx for sockets\n"
	"	                        (default: %s)\n"
	"	-M, --make-socket-dir make the socket directory\n"
	"	-O, --own-socket-dir  set user and group on socket directory\n"
	"\n"
	"	-h, --help            print this help and exit\n"
	"	-v, --version         print the version and exit\n"
	"\n"
;

static
const char
versiontxt[] =
	"cynagorad version "VERSION"\n"
;

static int isid(const char *text);
static void ensure_directory(const char *path, int uid, int gid);
static int lockdir(const char *dir);

int main(int ac, char **av)
{
	int opt;
	int rc;
	int makesockdir = 0;
	int makedbdir = 0;
	int owndbdir = 0;
	int ownsockdir = 0;
	int flog = 0;
	int help = 0;
	int version = 0;
	int error = 0;
	int uid = -1;
	int gid = -1;
	mode_t um;
	const char *init = NULL;
	const char *dbdir = NULL;
	const char *socketdir = NULL;
	const char *user = NULL;
	const char *group = NULL;
	struct passwd *pw;
	struct group *gr;
	cap_t caps = { 0 };
	cyn_server_t *server;
	char *spec_socket_admin, *spec_socket_check, *spec_socket_agent;

	setlinebuf(stdout);
	setlinebuf(stderr);

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
		case _LOG_:
			flog = 1;
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
	if (help) {
		fprintf(stdout, helptxt, cyn_default_socket_dir);
		return 0;
	}
	if (version) {
		fprintf(stdout, versiontxt);
		return 0;
	}
	if (error)
		return 1;

	/* set the defaults */
	dbdir = dbdir ?: DEFAULT_DB_DIR;
	socketdir = socketdir ?: cyn_default_socket_dir;
	user = user ?: DEFAULT_CYNAGORA_USER;
	group = group ?: DEFAULT_CYNAGORA_GROUP;
	init = init ?: DEFAULT_INIT_FILE;

	/* activate the agents */
	agent_at_activate();

	/* compute socket specs */
	spec_socket_admin = spec_socket_check = spec_socket_agent = 0;
#if defined(WITH_SYSTEMD)
	{
		char **names = 0;
		rc = sd_listen_fds_with_names(0, &names);
		if (rc >= 0 && names) {
			for (rc = 0 ; names[rc] ; rc++) {
				if (!strcmp(names[rc], "admin"))
					spec_socket_admin = strdup("sd:admin");
				else if (!strcmp(names[rc], "check"))
					spec_socket_check = strdup("sd:check");
				else if (!strcmp(names[rc], "agent"))
					spec_socket_agent = strdup("sd:agent");
				free(names[rc]);
			}
			free(names);
		}
	}
#endif
	if (!spec_socket_admin)
		rc = asprintf(&spec_socket_admin, "%s:%s/%s",
		              cyn_default_socket_scheme, socketdir, cyn_default_admin_socket_base);
	if (!spec_socket_check)
		rc = asprintf(&spec_socket_check, "%s:%s/%s",
		              cyn_default_socket_scheme, socketdir, cyn_default_check_socket_base);
	if (!spec_socket_agent)
		rc = asprintf(&spec_socket_agent, "%s:%s/%s",
		              cyn_default_socket_scheme, socketdir, cyn_default_agent_socket_base);
	if (!spec_socket_admin || !spec_socket_check || !spec_socket_agent) {
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
			uid = (int)pw->pw_uid;
			gid = (int)pw->pw_gid;
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
			gid = (int)gr->gr_gid;
		}
	}

	/* handle directories */
	um = umask(0077);
	if (makedbdir)
		ensure_directory(dbdir, owndbdir ? uid : -1, owndbdir ? gid : -1);
	umask(0022);
	if (makesockdir && socketdir[0] != '@')
		ensure_directory(socketdir, ownsockdir ? uid : -1, ownsockdir ? gid : -1);
	umask(um);

	/* drop privileges */
	if (gid >= 0) {
		rc = setgid((gid_t)gid);
		if (rc < 0) {
			fprintf(stderr, "can not change group: %s\n", strerror(errno));
			return -1;
		}
	}
	if (uid >= 0) {
		rc = setuid((uid_t)uid);
		if (rc < 0) {
			fprintf(stderr, "can not change user: %s\n", strerror(errno));
			return -1;
		}
	}
	cap_clear(caps);
	rc = cap_set_proc(caps);

	/* get lock for the database */
	rc = lockdir(dbdir);
	if (rc < 0) {
		fprintf(stderr, "can not lock database of directory %s: %s\n", dbdir, strerror(-rc));
		return 1;
	}

	/* connection to the database */
	rc = db_open(dbdir);
	if (rc < 0) {
		fprintf(stderr, "can not open database of directory %s: %s\n", dbdir, strerror(-rc));
		return 1;
	}

	/* initialisation of the database */
	if (db_is_empty()) {
		rc = db_import_path(init);
		if (rc < 0) {
			fprintf(stderr, "can't initialize database: %s\n", strerror(-rc));
			return 1;
		}
	}

	/* reset the change ids */
	cyn_changeid_reset();

	/* initialize server */
	setvbuf(stderr, NULL, _IOLBF, 1000);
	cyn_server_log = (bool)flog;
	signal(SIGPIPE, SIG_IGN); /* avoid SIGPIPE! */
	rc = cyn_server_create(&server, spec_socket_admin, spec_socket_check, spec_socket_agent);
	if (rc < 0) {
		fprintf(stderr, "can't initialize server: %s\n", strerror(-rc));
		return 1;
	}

	/* ready ! */
#if defined(WITH_SYSTEMD)
	sd_notify(0, "READY=1");
#endif

	/* serve */
	rc = cyn_server_serve(server);
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
					fprintf(stderr, "can not check %s: %s\n", path, strerror(errno));
					exit(1);
				} else if ((st.st_mode & S_IFMT) != S_IFDIR) {
					fprintf(stderr, "not a directory %s\n", path);
					exit(1);
				}
				/* set ownership */
				if (((uid_t)uid != st.st_uid && uid >= 0) || ((gid_t)gid != st.st_gid && gid >= 0)) {
					rc = chown(path, (uid_t)uid, (gid_t)gid);
					if (rc < 0) {
						fprintf(stderr, "can not own directory %s for uid=%d & gid=%d: %s\n", path, uid, gid, strerror(errno));
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
			fprintf(stderr, "can not ensure directory %s: %s\n", path, strerror(errno));
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
		fprintf(stderr, "path toooooo long (%s)\n", path);
		exit(1);
	}
	p = strndupa(path, l);
	ensuredir(p, (int)l, uid, gid);
}

static int lockdir(const char *dir)
{
	char path[PATH_MAX];
	int fd, rc;

	rc = snprintf(path, sizeof path, "%s/%s", dir, DEFAULT_LOCKFILE);
	if (rc >= (int)sizeof path) {
		rc = -1;
		errno = ENAMETOOLONG;
	} else if (rc >= 0) {
		rc = open(path, O_RDWR|O_CREAT, 0600);
		if (rc >= 0) {
			fd = rc;
			rc = flock(fd, LOCK_EX|LOCK_NB);
		}
	}
	return rc;
}

