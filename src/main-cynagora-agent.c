/*
 * Copyright (C) 2018-2024 IoT.bzh Company
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
/* IMPLEMENTATION OF CYNAGORA ADMINISTRATION TOOL                             */
/******************************************************************************/
/******************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/wait.h>

#include "cynagora.h"
#include "expire.h"

#define BUFFER_SIZE     500
#define ARGUMENT_COUNT   10
#define QUERY_COUNT      40
#define SUBQUERY_COUNT   80

#define FD_FOR_STDIN    -1
#define FD_FOR_CYNAGORA -2


#define _LONGHELP_    'H'
#define _HELP_        'h'
#define _SOCKET_      's'
#define _VERSION_     'v'
#define _PIPED_       'p'

static
const char
shortopts[] = "Hhps:v";

static
const struct option
longopts[] = {
	{ "long-help", 0, NULL, _LONGHELP_ },
	{ "help", 0, NULL, _HELP_ },
	{ "piped", 0, NULL, _PIPED_ },
	{ "socket", 1, NULL, _SOCKET_ },
	{ "version", 0, NULL, _VERSION_ },
	{ NULL, 0, NULL, 0 }
};

static
const char
helptxt[] =
	"\n"
	"usage: cynagora-agent [options]... name [program [args]...]\n"
	"\n"
	"options:\n"
	"   -s, --socket xxx      set the base xxx for sockets\n"
	"   -p, --piped           replace stdin/out by out/in of program\n"
	"   -h, --help            print short help and exit\n"
	"   -H, --long-help       print long help and exit\n"
	"   -v, --version         print the version and exit\n"
	"\n"
;

static
const char
longhelptxt[] =
	"When no program is given, cynagora-agent output queries as below\n"
	"\n"
	"    ID VALUE CLIENT SESSION USER PERMISSION\n"
	"\n"
	"where ID is a numeric identifier, VALUE is the value associated\n"
	"to the agent and client, session, user and permission are from the\n"
	"query.\n"
	"\n"
	"For the replies, it reads from its input:\n"
	"\n"
	"    ID (yes|no) [expire]\n"
	"\n"
	"For the sub queries, it reads from its input:\n"
	"\n"
	"    ID sub NUM CLIENT SESSION USER PERMISSION\n"
	"\n"
	"Where NUM is a numeric identifier. It will reply to sub queries with:\n"
	"\n"
	"    reply NUM (yes|no)\n"
	"\n"
	"When the option --piped is given, the input and output are connected\n"
	"to the output and input of the given program.\n"
	"\n"
	"When program is given but not the option --piped then an instance of\n"
	"program is invoked for each agent query with predefined environment\n"
	"variable set: \n"
	"       - CYAG_VALUE       value associated to the agent\n"
	"       - CYAG_CLIENT      client of the query\n"
	"       - CYAG_SESSION     session of the query\n"
	"       - CYAG_USER        user of the query\n"
	"       - CYAG_PERMISSION  permission of the query\n"
	"\n"
	"The program will reply\n"
	"\n"
	"    (yes|no) [expire]\n"
	"\n"
	"and then terminates quickly.\n"
	"It can also ask for sub-queries:\n"
	"\n"
	"    sub NUM CLIENT SESSION USER PERMISSION\n"
	"\n"
	"Where NUM is a numeric identifier. It will reply to sub queries with:\n"
	"\n"
	"    reply NUM (yes|no)\n"
	"\n"
	"And the program terminates quickly.\n"
	"\n"
;

static
const char
versiontxt[] =
	"cynagora-agent version "VERSION"\n"
;

static char *name;
static char **prog;
static int piped;
static cynagora_t *cynagora;

/******************************************************************************/
/******************************************************************************/

typedef struct {
	int filled;
	char buffer[BUFFER_SIZE];
} buf_t;

typedef struct {
	int count;
	char *args[ARGUMENT_COUNT];
} args_t;

int buf_parse(buf_t *buf, args_t *args)
{
	char *p, *x;
	size_t s;
	int r;
	static const char seps[] = " \t";

	p = memchr(buf->buffer, '\n', (size_t)buf->filled);
	if (!p)
		r = 0;
	else {
		/* process one line: split args */
		*p++ = 0;
		r = (int)(p - buf->buffer);

		args->count = 0;
		x = buf->buffer;
		s = strspn(x, seps);
		x = &x[s];
		while (*x) {
			if (args->count < (int)(sizeof args->args / sizeof *args->args))
				args->args[args->count++] = x;
			s = strcspn(x, seps);
			x = &x[s];
			if (!*x)
				break;
			*x++ = 0;
			s = strspn(x, seps);
			x = &x[s];
		}
	}
	return r;
}

void buf_unprefix(buf_t *buf, int count)
{
	int remain;
	if (count > 0) {
		remain = buf->filled - count;
		if (remain >= 0) {
			if (remain)
				memmove(buf->buffer, &buf->buffer[count], (size_t)remain);
			buf->filled = remain;
		}
	}
}

/* read the 'buf' from 'fd' and call 'fun' if line of args exists */
void read_and_dispatch(int fd, buf_t *buf, void (*fun)(int,char**))
{
	int n;
	ssize_t sz;
	args_t args;

	sz = read(fd, &buf->buffer[buf->filled], sizeof buf->buffer - (size_t)buf->filled);
	if (sz > 0) {
		buf->filled += (int)sz;

		n = buf_parse(buf, &args);
		while (n) {
			if (args.count)
				fun(args.count, args.args);
			buf_unprefix(buf, n);
			n = buf_parse(buf, &args);
		}
	}
}

/******************************************************************************/
/******************************************************************************/

typedef struct {
	cynagora_query_t *query;
	pid_t pid;
	int id;
	int io[2];
	buf_t buf;
} query_t;

typedef struct {
	int me;
	int id;
	int num;
} subq_t;

static query_t queries[QUERY_COUNT];
static subq_t subqs[SUBQUERY_COUNT];

static int nexid;
static int nexme;
static int qx;
static int efd;

int qidx(int id)
{
	int r = sizeof queries / sizeof *queries;
	while (r-- && queries[r].id != id);
	return r;
}

int pidx(pid_t pid)
{
	int r = sizeof queries / sizeof *queries;
	while (r-- && queries[r].pid != pid);
	return r;
}

int sidx(int me)
{
	int r = sizeof subqs / sizeof *subqs;
	while (r-- && subqs[r].me != me);
	return r;
}

/*
 * pipes and forks
 *
 * for the parent it returns the pid > 0 of the child and in io[0] the input
 * from child and in io[1] to output to child.
 *
 * for the child it return 0 and stdin (0) comes from the prent and stout (1)
 * outputs to parent (io is not used)
 *
 * returns -1 in case of error with errno appropriately
 */
pid_t split(int io[2])
{
	int rc;
	int parent2child[2], child2parent[2];
	pid_t pid = -1;

	/* create pipes */
	rc = pipe(parent2child);
	if (rc == 0) {
		rc = pipe(child2parent);
		if (rc == 0) {
			pid = fork();
			if (pid >= 0) {
				if (pid == 0) {
					/* in child */
					close(0);
					dup(parent2child[0]);
					close(1);
					dup(child2parent[1]);
				} else {
					/* in parent */
					io[0] = dup(child2parent[0]);
					io[1] = dup(parent2child[1]);
				}
			}
			close(child2parent[0]);
			close(child2parent[1]);
		}
		close(parent2child[0]);
		close(parent2child[1]);
	}
	return pid;
}

/* like printf but with fd and sync */
int emit(int fd, const char *fmt, ...)
{
	int n, w, p;
	va_list ap;
	char buffer[2000];

	va_start(ap, fmt);
	n = vsnprintf(buffer, sizeof buffer, fmt, ap);
	va_end(ap);

	if (n >= (int)sizeof buffer) {
		errno = EINVAL;
		return -1;
	}
	p = 0;
	while (p < n) {
		w = (int)write(fd, &buffer[p], (size_t)(n - p));
		if (w > 0)
			p += w;
		else if (errno != EINTR)
			return -1;
	}
	fsync(fd);
	return 0;

}

void clear(int id)
{
	int r;
	struct epoll_event e;

	r = sizeof subqs / sizeof *subqs;
	while (r)
		if (subqs[--r].id == id)
			memset(&subqs[r], 0, sizeof subqs[r]);

	r = sizeof queries / sizeof *queries;
	while (r)
		if (queries[--r].id == id) {
			if (queries[r].pid) {
				memset(&e, 0, sizeof e);
				epoll_ctl(efd, EPOLL_CTL_DEL, queries[r].io[0], &e);
				close(queries[r].io[0]);
				close(queries[r].io[1]);
			}
			memset(&queries[r], 0, sizeof queries[r]);
		}
}

int reply(int q, char *diag, char *expire)
{
	cynagora_value_t value;
	cynagora_query_t *query;

	query = queries[q].query;
	value.value = strdupa(diag ?: "no");
	txt2exp(expire ?: "*", &value.expire, true);
	clear(queries[q].id);
	return cynagora_agent_reply(query, &value);
}

void terminate(int id)
{
	int q;

	if (id) {
		q = qidx(id);
		if (q >= 0)
			reply(q, NULL, NULL);
		else
			clear(id);
	}
}

void on_subquery_reply(void *closure, int status)
{
	int me, s, q, fd;


	me = (int)(intptr_t)closure;
	s = sidx(me);
	if (s >= 0) {
		q = qidx(subqs[s].id);
		if (q > 0) {
			fd = prog ? queries[q].io[1] : 1;
			emit(fd, "reply %d %s\n", subqs[s].num, status ? "yes" : "no");
		}
		memset(&subqs[s], 0, sizeof subqs[s]);
	}

}

int subquery(int q, int num,  char *client, char *session, char *user, char *permission)
{
	int rc, me, s;
	cynagora_key_t key;

	/* get an id */
	do {
		me = ++nexme;
		if (me < 0)
			me = nexme = 1;
	} while (sidx(me) >= 0);

	s = sidx(0);
	subqs[s].me = me;
	subqs[s].id = queries[q].id;
	subqs[s].num = num;

	key.client = client ?: "?";
	key.session = session ?: "?";
	key.user = user ?: "?";
	key.permission = permission ?: "?";

	rc = cynagora_agent_subquery_async(queries[q].query, &key, 0, on_subquery_reply, (void*)(intptr_t)me);
	return rc;
}

int launch(int q)
{
	int rc;
	int io[2];
	pid_t pid;

	pid = split(io);
	if (pid < 0)
		rc = -1;
	else if (pid == 0) {
		setenv("CYAG_VALUE", queries[q].query->value, 1);
		setenv("CYAG_CLIENT", queries[q].query->key.client, 1);
		setenv("CYAG_SESSION", queries[q].query->key.session, 1);
		setenv("CYAG_USER", queries[q].query->key.user, 1);
		setenv("CYAG_PERMISSION", queries[q].query->key.permission, 1);
		execvp(prog[0], prog);
		emit(2, "error: can't exec %s: %s\n", prog[0], strerror(errno));
		exit(1);
	} else {
		queries[q].pid = pid;
		queries[q].io[0] = io[0];
		queries[q].io[1] = io[1];
		rc = 0;
	}
	return rc;
}

void dispatch(int q, int ac, char **av)
{
	if (q < 0)
		return;

	if (ac < 1 || strcmp(av[0], "sub")) {
		reply(q, av[0], ac > 1 ? av[1] : NULL);
	} else {
		subquery(q, ac > 1 ? atoi(av[1]) : 1,
			ac > 2 ? av[2] : NULL,
			ac > 3 ? av[3] : NULL,
			ac > 4 ? av[4] : NULL,
			ac > 5 ? av[5] : NULL);
	}
}

void dispatch_direct(int ac, char **av)
{
	int q, qid;

	qid = atoi(av[0]);
	q = qidx(qid);
	if (q < 0)
		return;

	dispatch(q, ac - 1, &av[1]);
}

void dispatch_fork(int ac, char **av)
{
	dispatch(qx, ac, av);
}

void process_fork(int id)
{
	int q = qidx(id);
	if (q >= 0) {
		qx = q;
		read_and_dispatch(queries[q].io[0], &queries[q].buf, dispatch_fork);
	}
}

/* handles death of a child */
void deadchild(int sig, siginfo_t *info, void *item)
{
	pid_t pid;

	if (piped) {
		exit(info->si_code == CLD_EXITED ? info->si_status : 127);
		return;
	}

	pid = info->si_pid;
/*
	int q, id;

	q = pidx(pid);
	if (q >= 0) {
		id = queries[q].id;
		qx = q;
		read_and_dispatch(queries[q].io[0], &queries[q].buf, dispatch_fork);
		terminate(id);
	}
*/
	waitpid(pid, NULL, 0);
}

int setup_deadchild()
{
	struct sigaction sigact;

	/* set the signal handler */
	sigact.sa_sigaction = deadchild;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
	return sigaction(SIGCHLD, &sigact, NULL);
}

int run_piped_program()
{
	int rc, io[2];
	pid_t pid;

	/* fork the child */
	pid = split(io);
	if (pid < 0)
		return -1;

	if (pid) {
		close(0);
		dup(io[0]);
		close(io[0]);
		close(1);
		dup(io[1]);
		close(io[1]);
		return 0;
	}

	/* run piped if required */
	rc = execvp(prog[0], prog);
	emit(2, "error: can't exec %s: %s\n", prog[0], strerror(errno));
	exit(!!rc);
	return rc;
}

int async_ctl(void *closure, int op, int fd, uint32_t events)
{
	struct epoll_event e;
	memset(&e, 0, sizeof e);
	e.events = events;
	e.data.fd = FD_FOR_CYNAGORA;
	return epoll_ctl(efd, op, fd, &e);
}

int agentcb(void *closure, cynagora_query_t *query)
{
	int q, id, rc;
	struct epoll_event e;

	/* get an id */
	do {
		id = ++nexid;
		if (id < 0)
			id = nexid = 1;
	} while (qidx(id) >= 0);

	/* get an index */
	q = qidx(0);
	if (q < 0)
		return -ECANCELED;

	queries[q].id = id;
	queries[q].query = query;

	/* compose the value */
	if (prog) {
		rc = launch(q);
		if (rc == 0) {
			memset(&e, 0, sizeof e);
			e.events = EPOLLIN;
			e.data.fd = id;
			rc = epoll_ctl(efd, EPOLL_CTL_ADD, queries[q].io[0], &e);
		}
	} else {
		rc = emit(1, "%d %s %s %s %s %s\n",
			id, query->value, query->key.client, query->key.session,
			query->key.user, query->key.permission);
	}
	if (rc < 0) {
		clear(id);
		return -ECANCELED;
	}

	return 0;
}

int main(int ac, char **av)
{
	int opt;
	int rc;
	int help = 0;
	int version = 0;
	int error = 0;
	struct epoll_event e;
	char *socket = NULL;
	buf_t buffer = { .filled = 0 };

	/* scan arguments */
	for (;;) {
		opt = getopt_long(ac, av, shortopts, longopts, NULL);
		if (opt == -1)
			break;

		switch(opt) {
		case _LONGHELP_:
			help = 2;
			break;
		case _HELP_:
			help++;
			break;
		case _PIPED_:
			piped = 1;
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
		printf("%s", helptxt);
		if (help > 1)
			printf("%s", longhelptxt);
		return 0;
	}
	if (version) {
		printf("%s", versiontxt);
		return 0;
	}

	/* check agent name */
	if (optind == ac) {
		emit(2, "error: name missing\n");
		error = 1;
	} else {
		name = av[optind++];
		if (!cynagora_agent_is_valid_name(name)) {
			emit(2, "error: invalid agent name %s\n", name);
			error = 1;
		} else if (optind < ac) {
			prog = &av[optind++];
		}else if (piped) {
			emit(2, "error: piped without program\n");
			error = 1;
		} else {
			prog = NULL;
		}
	}
	if (error)
		return 1;

	/* create the polling */
	efd = epoll_create1(EPOLL_CLOEXEC);
	if (efd < 0) {
		emit(2, "error: epoll_create failed, %s\n", strerror(errno));
		return 1;
	}

	/* initialize server */
	signal(SIGPIPE, SIG_IGN); /* avoid SIGPIPE! */
	rc = cynagora_create(&cynagora, cynagora_Agent, 0, socket);
	if (rc < 0) {
		emit(2, "error: initialization failed, %s\n", strerror(-rc));
		return 1;
	}
	rc = cynagora_async_setup(cynagora, async_ctl, NULL);
	if (rc < 0) {
		emit(2, "error: asynchronous setup failed, %s\n", strerror(-rc));
		return 1;
	}

	/* create the agent */
	rc = cynagora_agent_create(cynagora, name, agentcb, NULL);
	if (rc < 0) {
		emit(2, "error: creation of agent failed, %s\n", strerror(-rc));
		return 1;
	}

	/* setup piped */
	setup_deadchild();
	if (piped) {
		rc = run_piped_program();
		if (rc < 0) {
			emit(2, "error: can't run piped program, %s\n", strerror(errno));
			return 1;
		}
		prog = NULL;
	}

	/* catch input if needed */
	if (!prog) {
		memset(&e, 0, sizeof e);
		e.events = EPOLLIN;
		e.data.fd = FD_FOR_STDIN;
		rc = epoll_ctl(efd, EPOLL_CTL_ADD, 0, &e);
		if (rc < 0) {
			emit(2, "error: set epoll, %s\n", strerror(errno));
			return 1;
		}
	}

	for(;;) {
		rc = epoll_wait(efd, &e, 1, -1);
		if (rc == 1) {
			if (e.events & EPOLLIN) {
				if (e.data.fd == FD_FOR_STDIN) {
					read_and_dispatch(0, &buffer, dispatch_direct);
				} else if (e.data.fd == FD_FOR_CYNAGORA) {
					rc = cynagora_async_process(cynagora);
					if (rc < 0)
						emit(2, "asynchronous processing failed: %s\n", strerror(-rc));
				} else {
					process_fork(e.data.fd);
				}
			}
			if (e.events & EPOLLHUP) {
				if (e.data.fd == FD_FOR_STDIN) {
					break;
				} else if (e.data.fd == FD_FOR_CYNAGORA) {
					break;
				} else {
					terminate(e.data.fd);
				}
			}
		}
	}
	return 0;
}
