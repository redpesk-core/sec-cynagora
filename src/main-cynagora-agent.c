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
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF CYNAGORA ADMINISTRATION TOOL                             */
/******************************************************************************/
/******************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
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

#define _HELP_        'h'
#define _SOCKET_      's'
#define _VERSION_     'v'
#define _PIPED_       'p'

static
const char
shortopts[] = "hps:v";

static
const struct option
longopts[] = {
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
	"usage: cynagora-agent [options]... name [program]\n"
	"\n"
	"otpions:\n"
	"	-s, --socket xxx      set the base xxx for sockets\n"
	"       -p, --piped           replace stdin/out by out/in of program"
	"	-h, --help            print this help and exit\n"
	"	-v, --version         print the version and exit\n"
	"\n"
	"When program is given, cynagora-agent performs invoke it with\n"
	"arguments VALUE CLIENT SESSION USER PERMISSION and expect it\n"
	"to echo the result with optional expiration\n"
	"Otherwise cynagora-agent echo its requests on one line:\n"
	"ID VALUE CLIENT SESSION USER PERMISSION and expect to read the\n"
	"replies: ID RESULT [EXPIRE]\n"
	"\n"
;

static
const char
versiontxt[] =
	"cynagora-agent version 1.99.99\n"
;

typedef struct {
	int filled;
	char buffer[4000];
} buf_t;

typedef struct {
	int count;
	char *args[20];
} args_t;

typedef struct {
	int id;
	cynagora_query_t *query;
} query_t;

typedef struct {
	pid_t pid;
	int id;
	int io[2];
} proc_t;

static char *name;
static char **prog;
static int piped;
static cynagora_t *cynagora;
static int nexid;

static buf_t buffer;
static query_t queries[200];
static proc_t procs[200];

int qidx(int id)
{
	int r = sizeof queries / sizeof *queries;
	while (r-- && queries[r].id != id);
	return r;
}

int pidx(pid_t pid)
{
	int r = sizeof procs / sizeof *procs;
	while (r-- && procs[r].pid != pid);
	return r;
}

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

void deadchild(int sig, siginfo_t *info, void *item)
{
	int i;
	pid_t pid;
	buf_t buf;
	args_t args;
	ssize_t sz;

	pid = info->si_pid;
	i = pidx(pid);
	if (i >= 0) {
		args.count = 0;
		sz = read(procs[i].io[0], buf.buffer, sizeof buf.buffer);
		if (sz > 0) {
			buf.filled = (int)sz;
			buf_parse(&buf, &args);
		}
		if (!args.count) {
			args.args[0] = "no";
			args.args[1] = "-";
			args.count = 2;
		}
		if (args.count == 1)
			printf("%d %s\n", procs[i].id, args.args[0]);
		else
			printf("%d %s %s\n", procs[i].id, args.args[0], args.args[1]);
		fflush(stdout);
		close(procs[i].io[0]);
		close(procs[i].io[1]);
		procs[i].pid = 0;
	}
	waitpid(pid, NULL, 0);
}

void onquery(int ac, char **av)
{
	int i;
	pid_t pid;

	i = pidx(0);
	if (ac == 6 && i >= 0) {
		procs[i].pid = pid = split(procs[i].io);
		if (pid >= 0) {
			procs[i].id = atoi(av[0]);
			if (!pid) {
				setenv("CYAG_VALUE", av[1], 1);
				setenv("CYAG_CLIENT", av[2], 1);
				setenv("CYAG_SESSION", av[3], 1);
				setenv("CYAG_USER", av[4], 1);
				setenv("CYAG_PERMISSION", av[5], 1);
				execvp(prog[0], prog);
				fprintf(stderr, "error: can't exec %s: %s\n", prog[0], strerror(errno));
				exit(1);
			}
			return;
			close(procs[i].io[0]);
			close(procs[i].io[1]);
		}
		procs[i].pid = 0;
	}
	fprintf(stdout, "%s no -\n", av[0]);
}

int runloop()
{
	struct pollfd pfd;
	struct sigaction sigact;

	/* set the signal handler */
	sigact.sa_sigaction = deadchild;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
	sigaction(SIGCHLD, &sigact, NULL);

	pfd.fd = 0;
	pfd.events = POLLIN;
	for(;;) {
		pfd.revents = 0;
		poll(&pfd, 1, -1);
		if (pfd.revents & POLLIN)
			read_and_dispatch(0, &buffer, onquery);
		if (pfd.revents & POLLHUP)
			break;
	}

	return 0;
}

int setup_child()
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
	if (piped) {
		rc = execvp(prog[0], prog);
		fprintf(stderr, "error: can't exec %s: %s\n", prog[0], strerror(errno));
	} else {
		rc = runloop();
		if (rc)
			fprintf(stderr, "error: can't loop: %s\n", strerror(errno));
	}
	exit(!!rc);
}

int agentcb(void *closure, cynagora_query_t *query)
{
	int i, id, rc;

	/* get an id */
	do {
		id = ++nexid;
		if (id < 0)
			id = nexid = 1;
	} while (qidx(id) >= 0);

	/* get an index */
	i = qidx(0);
	if (i < 0)
		return -ECANCELED;

	queries[i].id = id;
	queries[i].query = query;

	/* compose the value */
	rc = fprintf(stdout, "%d %s %s %s %s %s\n",
		id, query->value, query->key.client, query->key.session,
		query->key.user, query->key.permission);
	if (rc < 0) {
		queries[i].query = NULL;
		queries[i].id = 0;
		return -ECANCELED;
	}

	return 0;
}

void onreply(int ac, char **av)
{
	int i, id;
	cynagora_value_t value;

	id = atoi(av[0]);
	i = qidx(id);
	if (i >= 0) {
		value.value = "no";
		value.expire = 0;
		if (ac > 1)
			value.value = av[1];
		if (ac > 2)
			txt2exp(av[2], &value.expire, true);
		cynagora_agent_reply(queries[i].query, &value);
		queries[i].query = NULL;
		queries[i].id = 0;
	}
}

int async_ctl(void *closure, int op, int fd, uint32_t events)
{
	int *pfd = closure;

	switch(op) {
	case EPOLL_CTL_ADD:
	case EPOLL_CTL_MOD:
		*pfd = fd;
		break;
	case EPOLL_CTL_DEL:
		*pfd = -1;
		break;
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
	char *socket = NULL;
	struct pollfd fds[2];

	/* scan arguments */
	for (;;) {
		opt = getopt_long(ac, av, shortopts, longopts, NULL);
		if (opt == -1)
			break;

		switch(opt) {
		case _HELP_:
			help = 1;
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
		fprintf(stdout, helptxt);
		return 0;
	}
	if (version) {
		fprintf(stdout, versiontxt);
		return 0;
	}

	/* check agent name */
	if (optind == ac) {
		fprintf(stderr, "error: name missing\n");
		error = 1;
	} else {
		name = av[optind++];
		if (!cynagora_agent_is_valid_name(name)) {
			fprintf(stderr, "error: invalid agent name %s\n", name);
			error = 1;
		} else if (optind == ac) {
			prog = NULL;
		} else {
			prog = &av[optind++];
		}
	}
	if (error)
		return 1;

	/* initialize server */
	signal(SIGPIPE, SIG_IGN); /* avoid SIGPIPE! */
	rc = cynagora_create(&cynagora, cynagora_Agent, 0, socket);
	if (rc < 0) {
		fprintf(stderr, "error: initialization failed, %s\n", strerror(-rc));
		return 1;
	}
	fds[1].fd = -1;
	rc = cynagora_async_setup(cynagora, async_ctl, &fds[1].fd);
	if (rc < 0) {
		fprintf(stderr, "error: asynchronous setup failed, %s\n", strerror(-rc));
		return 1;
	}

	/* create the agent */
	rc = cynagora_agent_create(cynagora, name, agentcb, NULL);
	if (rc < 0) {
		fprintf(stderr, "error: creation of agent failed, %s\n", strerror(-rc));
		return 1;
	}

	/* setup piped */
	if (prog) {
		rc = setup_child();
		if (rc < 0) {
			fprintf(stderr, "error: can't setup child, %s\n", strerror(errno));
			return 1;
		}
	}

	/* setup output */
	setlinebuf(stdout);

	fcntl(0, F_SETFL, O_NONBLOCK);
	fds[0].fd = 0;
	fds[0].events = fds[1].events = POLLIN;
	for(;;) {
		rc = poll(fds, 2, -1);
		if (fds[0].revents & POLLIN)
			read_and_dispatch(0, &buffer, onreply);
		if (fds[1].revents & POLLIN) {
			rc = cynagora_async_process(cynagora);
			if (rc < 0)
				fprintf(stderr, "asynchronous processing failed: %s\n", strerror(-rc));
		}
		if (fds[0].revents & POLLHUP)
			break;
		if (fds[1].revents & POLLHUP)
			break;
	}
	return 0;
}
