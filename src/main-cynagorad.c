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
#include "expire.h"
#include "cyn-server.h"
#include "cyn-protocol.h"
#include "db-import.h"
#include "agent-at.h"
#include "settings.h"

#if !defined(DEFAULT_LOCKFILE)
#    define  DEFAULT_LOCKFILE      ".cynagora-lock"
#endif

#define _OFFLINE_     '\001'
#define _NO_CONFIG_   'C'
#define _CONFIG_      'c'
#define _DUMP_        'D'
#define _DBDIR_       'd'
#define _FORCEINIT_   'f'
#define _GROUP_       'g'
#define _HELP_        'h'
#define _INIT_        'i'
#define _LOG_         'l'
#define _MAKEDBDIR_   'm'
#define _MAKESOCKDIR_ 'M'
#define _OWNSOCKDIR_  'O'
#define _OWNDBDIR_    'o'
#define _SOCKETDIR_   'S'
#define _USER_        'u'
#define _VERSION_     'v'

static
const char
shortopts[] = "Cc:Dd:fg:hi:lmMOoS:u:v";

static
const struct option
longopts[] = {
	{ "config", 1, NULL, _CONFIG_ },
	{ "dbdir", 1, NULL, _DBDIR_ },
	{ "dump", 0, NULL, _DUMP_ },
	{ "force-init", 0, NULL, _FORCEINIT_ },
	{ "group", 1, NULL, _GROUP_ },
	{ "help", 0, NULL, _HELP_ },
	{ "init", 1, NULL, _INIT_ },
	{ "log", 0, NULL, _LOG_ },
	{ "make-db-dir", 0, NULL, _MAKEDBDIR_ },
	{ "make-socket-dir", 0, NULL, _MAKESOCKDIR_ },
	{ "no-config", 0, NULL, _NO_CONFIG_ },
	{ "offline", 0, NULL, _OFFLINE_ },
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
	"	-c, --config xxx      use configuration file xxx\n"
	"	                        (default: "DEFAULT_CONFIG_FILE"\n"
	"	-C, --no-config       dont read any config file\n"
	"	-u, --user xxx        set the user\n"
	"	-g, --group xxx       set the group\n"
	"	-f, --force-init      always set initialization rules\n"
	"	-i, --init xxx        initialize if needed the database with file xxx\n"
	"	                        (default: "DEFAULT_INIT_FILE"\n"
	"	    --offline         add rules from stdin and exit\n"
	"	-D, --dump            dump current rules to stdout and exit\n"
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
static void dumpdb(FILE *fout);

int main(int ac, char **av)
{
	int opt;
	int rc;
	int flog = 0;
	int help = 0;
	int dump = 0;
	int offline = 0;
	int version = 0;
	int error = 0;
	int uid = -1;
	int gid = -1;
	mode_t um;
	int noconfig = 0;
	const char *config = NULL;
	settings_t settings;
	struct passwd *pw;
	struct group *gr;
	cap_t caps = { 0 };
	cyn_server_t *server;
	char *spec_socket_admin, *spec_socket_check, *spec_socket_agent;

	setlinebuf(stdout);
	setlinebuf(stderr);

	/* scan arguments first pass */
	for (;;) {
		opt = getopt_long(ac, av, shortopts, longopts, NULL);
		if (opt == -1)
			break;

		switch(opt) {
		case _CONFIG_:
			config = optarg;
			break;
		case _HELP_:
			help = 1;
			break;
		case _NO_CONFIG_:
			noconfig = 1;
			break;
		case _VERSION_:
			version = 1;
			break;
		case _DUMP_:
		case _DBDIR_:
		case _FORCEINIT_:
		case _GROUP_:
		case _INIT_:
		case _LOG_:
		case _MAKEDBDIR_:
		case _MAKESOCKDIR_:
		case _OFFLINE_:
		case _OWNSOCKDIR_:
		case _OWNDBDIR_:
		case _SOCKETDIR_:
		case _USER_:
			break;
		default:
			error = 1;
			break;
		}
	}

	/* handles help, version, error */
	if (help) {
		fprintf(stdout, helptxt, cyn_default_socket_dir);
		return EXIT_SUCCESS;
	}
	if (version) {
		fprintf(stdout, versiontxt);
		return EXIT_SUCCESS;
	}
	if (error)
		return EXIT_FAILURE;

	/* set the defaults */
	initialize_default_settings(&settings);
	if (!noconfig) {
		rc = read_file_settings(&settings, config);
		if (rc < 0) {
			fprintf(stderr, "can't read config file\n");
			return EXIT_FAILURE;
		}
	}

	/* scan arguments second pass */
	optind = 1;
	for (;;) {
		opt = getopt_long(ac, av, shortopts, longopts, NULL);
		if (opt == -1)
			break;

		switch(opt) {
		case _DUMP_:
			dump = 1;
			break;
		case _DBDIR_:
			settings.dbdir = optarg;
			break;
		case _FORCEINIT_:
			settings.forceinit = 1;
			break;
		case _GROUP_:
			settings.group = optarg;
			break;
		case _INIT_:
			settings.init = optarg;
			break;
		case _LOG_:
			flog = 1;
			break;
		case _MAKEDBDIR_:
			settings.makedbdir = 1;
			break;
		case _MAKESOCKDIR_:
			settings.makesockdir = 1;
			break;
		case _OFFLINE_:
			offline = 1;
			break;
		case _OWNSOCKDIR_:
			settings.ownsockdir = 1;
			break;
		case _OWNDBDIR_:
			settings.owndbdir = 1;
			break;
		case _SOCKETDIR_:
			settings.socketdir = optarg;
			break;
		case _USER_:
			settings.user = optarg;
			break;
		default:
			break;
		}
	}

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
		              cyn_default_socket_scheme, settings.socketdir, cyn_default_admin_socket_base);
	if (!spec_socket_check)
		rc = asprintf(&spec_socket_check, "%s:%s/%s",
		              cyn_default_socket_scheme, settings.socketdir, cyn_default_check_socket_base);
	if (!spec_socket_agent)
		rc = asprintf(&spec_socket_agent, "%s:%s/%s",
		              cyn_default_socket_scheme, settings.socketdir, cyn_default_agent_socket_base);
	if (!spec_socket_admin || !spec_socket_check || !spec_socket_agent) {
		fprintf(stderr, "can't make socket paths\n");
		return EXIT_FAILURE;
	}

	/* compute user and group */
	if (settings.user) {
		uid = isid(settings.user);
		if (uid < 0) {
			pw = getpwnam(settings.user);
			if (pw == NULL) {
				fprintf(stderr, "can not find user '%s'\n", settings.user);
				return EXIT_FAILURE;
			}
			uid = (int)pw->pw_uid;
			gid = (int)pw->pw_gid;
		}
	}
	if (settings.group) {
		gid = isid(settings.group);
		if (gid < 0) {
			gr = getgrnam(settings.group);
			if (gr == NULL) {
				fprintf(stderr, "can not find group '%s'\n", settings.group);
				return EXIT_FAILURE;
			}
			gid = (int)gr->gr_gid;
		}
	}

	/* handle directories */
	um = umask(0077);
	if (settings.makedbdir)
		ensure_directory(settings.dbdir, settings.owndbdir ? uid : -1, settings.owndbdir ? gid : -1);
	umask(0022);
	if (settings.makesockdir && settings.socketdir[0] != '@')
		ensure_directory(settings.socketdir, settings.ownsockdir ? uid : -1, settings.ownsockdir ? gid : -1);
	umask(um);

	/* drop privileges */
	if (gid >= 0) {
		rc = setgid((gid_t)gid);
		if (rc < 0) {
			fprintf(stderr, "can not change group: %s\n", strerror(errno));
			return EXIT_FAILURE;
		}
	}
	if (uid >= 0) {
		rc = setuid((uid_t)uid);
		if (rc < 0) {
			fprintf(stderr, "can not change user: %s\n", strerror(errno));
			return EXIT_FAILURE;
		}
	}
	cap_clear(caps);
	rc = cap_set_proc(caps);

	/* get lock for the database */
	rc = lockdir(settings.dbdir);
	if (rc < 0) {
		/* the lock on the database means that a server already runs */
		if (!offline) {
			fprintf(stderr, "can not lock database of directory %s: %s\n", settings.dbdir, strerror(-rc));
			return EXIT_FAILURE;
		}

		/* when offline isn't really offline, try client program */
		fprintf(stderr, "probably not offline, trying admin client\n");
		rc = system("sed 's/^/set /' | cynagora-admin");
		if (rc == 0)
			return EXIT_SUCCESS; /* no error */

		/* report the error */
		if (rc < 0)
			fprintf(stderr, "exec client error: %s\n", strerror(errno));
		else if (rc == 127)
			fprintf(stderr, "can not spawn the client\n");
		else
			fprintf(stderr, "client returned error %d\n", rc);
		return EXIT_FAILURE;
	}

	/* connection to the database */
	rc = db_open(settings.dbdir);
	if (rc < 0) {
		fprintf(stderr, "can not open database of directory %s: %s\n", settings.dbdir, strerror(-rc));
		return EXIT_FAILURE;
	}

	/* initialisation of the database */
	if (settings.forceinit || db_is_empty()) {
		rc = db_import_path(settings.init);
		if (rc < 0) {
			fprintf(stderr, "can't initialize database: %s\n", strerror(-rc));
			return EXIT_FAILURE;
		}
	}

	/* dumps the current rules to the standard output */
	if (dump) {
		dumpdb(stdout);
		return EXIT_SUCCESS;
	}

	/* when offline only add rules from stdin */
	if (offline) {
		rc = db_import_file(stdin, NULL);
		if (rc < 0) {
			fprintf(stderr, "can't add rules: %s\n", strerror(-rc));
			return EXIT_FAILURE;
		}
		return EXIT_SUCCESS;
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
		return EXIT_FAILURE;
	}

	/* ready ! */
#if defined(WITH_SYSTEMD)
	sd_notify(0, "READY=1");
#endif

	/* serve */
	rc = cyn_server_serve(server);
	return rc ? EXIT_FAILURE : EXIT_SUCCESS;
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

/* set the lockfile in the given directory
 * return 0 on success otherwise a negative value */
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

/* callback dumping one rule */
static void dumpcb(void *closure, const data_key_t *key, const data_value_t *value)
{
	char buffer[50];
	FILE *fout = closure;

	exp2txt(value->expire, true, buffer, sizeof buffer);
	buffer[sizeof buffer - 1] = 0;
	fprintf(fout, "%s %s %s %s %s %s\n",
		key->client,
		key->session,
		key->user,
		key->permission,
		value->value,
		buffer);
}

/* dump the content of the db to fout */
static void dumpdb(FILE *fout)
{
	data_key_t key = {
		.client     = Data_Any_String,
		.session    = Data_Any_String,
		.user       = Data_Any_String,
		.permission = Data_Any_String
	};

	db_for_all(dumpcb, fout, &key);
}
