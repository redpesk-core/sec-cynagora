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
/* IMPLEMENTATION OF SERVER PART OF CYNAGORA-PROTOCOL                         */
/******************************************************************************/
/******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <limits.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "data.h"
#include "prot.h"
#include "cyn.h"
#include "cyn-protocol.h"
#include "cyn-server.h"
#include "socket.h"
#include "pollitem.h"

#define MAX_PUTX_ITEMS 15

/** should log? */
bool
cyn_server_log = 0;

/** local enumeration of socket/client kind */
typedef enum server_type {
	server_Check,
	server_Agent,
	server_Admin
} server_type_t;

/** structure that represents a client */
struct client
{
	/** a protocol structure */
	prot_t *prot;

	/** type of client */
	unsigned type: 2;

	/** the version of the protocol used */
	unsigned version: 4;

	/** is relaxed version of the protocol */
	unsigned relax: 1;

	/** is the actual link invalid or valid */
	unsigned invalid: 1;

	/** enter/leave status, record if entered */
	unsigned entered: 1;

	/** enter/leave status, record if enter is pending */
	unsigned entering: 1;

	/** indicate if some check were made */
	unsigned checked: 1;

	/** polling callback */
	pollitem_t pollitem;
};
typedef struct client client_t;

/** structure for servers */
struct cyn_server
{
	/** the pollfd to use */
	int pollfd;

	/** is stopped ? */
	int stopped;

	/** the admin socket */
	pollitem_t admin;

	/** the agent socket */
	pollitem_t agent;

	/** the check socket */
	pollitem_t check;
};

static
void
dolog(
	client_t *cli,
	int c2s,
	int count,
	const char *fields[]
) {
	static const char types[3][6] = { "check", "agent", "admin" };
	static const char dir[2] = { '>', '<' };

	int i;

	fprintf(stderr, "%p%c%c%s", cli, dir[!c2s], dir[!c2s], types[cli->type]);
	for (i = 0 ; i < count ; i++)
		fprintf(stderr, " %s", fields[i]);
	fprintf(stderr, "\n");
}

/**
 * Check 'arg' against 'value' beginning at offset accepting it if 'arg' prefixes 'value'
 * Returns 1 if matching or 0 if not.
 */
static
bool
ckarg(
	const char *arg,
	const char *value,
	unsigned offset
) {
	while(arg[offset])
		if (arg[offset] == value[offset])
			offset++;
		else
			return false;
	return true;
}

/**
 * Flush the write buffer
 */
static
int
flushw(
	client_t *cli
) {
	int rc;
	struct pollfd pfd;

	for(;;) {
		rc = prot_should_write(cli->prot);
		if (!rc)
			break;
		rc = prot_write(cli->prot, cli->pollitem.fd);
		if (rc == -EAGAIN) {
			pfd.fd = cli->pollitem.fd;
			pfd.events = POLLOUT;
			do { rc = poll(&pfd, 1, 0); } while (rc < 0 && errno == EINTR);
			if (rc < 0)
				rc = -errno;
		}
		if (rc < 0)
			break;
	}
	return rc;
}

/**
 * Send a reply to client
 */
static
int
putx(
	client_t *cli,
	...
) {
	const char *p, *fields[MAX_PUTX_ITEMS];
	unsigned n;
	va_list l;
	int rc;

	/* store temporary in fields */
	n = 0;
	va_start(l, cli);
	p = va_arg(l, const char *);
	while (p) {
		if (n == MAX_PUTX_ITEMS)
			return -EINVAL;
		fields[n++] = p;
		p = va_arg(l, const char *);
	}
	va_end(l);

	/* emit the log */
	if (cyn_server_log)
		dolog(cli, 0, n, fields);

	/* send now */
	rc = prot_put(cli->prot, n, fields);
	if (rc == -ECANCELED) {
		rc = flushw(cli);
		if (rc == 0)
			rc = prot_put(cli->prot, n, fields);
	}
	return rc;
}

/** emit a simple done reply and flush */
static
void
send_done(
	client_t *cli
) {
	putx(cli, _done_, NULL);
	flushw(cli);
}

/** emit a simple error reply and flush */
static
void
send_error(
	client_t *cli,
	const char *errorstr
) {
	putx(cli, _error_, errorstr, NULL);
	flushw(cli);
}

/** emit a simple done/error reply */
static
void
send_done_or_error(
	client_t *cli,
	int status
) {
	if (status >= 0)
		send_done(cli);
	else
		send_error(cli, NULL);
}

/** callback of entering */
static
void
entercb(
	void *closure
) {
	client_t *cli = closure;

	cli->entered = true;
	cli->entering = false;
	send_done(cli);
}

/** translate optional expire value */
static
const char *
exp2txt(
	time_t expire,
	char *buffer,
	size_t bufsz
) {
	if (!expire)
		return NULL;

	/* TODO: check size */
	snprintf(buffer, bufsz, "%lld", (long long)expire);
	return buffer;
}

/** callback of checking */
static
void
testcheckcb(
	void *closure,
	const data_value_t *value,
	bool ischeck
) {
	client_t *cli = closure;
	char text[30];
	const char *etxt, *vtxt;

	if (!value) {
		vtxt = _no_;
		etxt = 0;
	} else {
		if (!strcmp(value->value, ALLOW))
			vtxt = _yes_;
		else if (!strcmp(value->value, DENY) || ischeck)
			vtxt = _no_;
		else
			vtxt = _done_;
		etxt = exp2txt(value->expire, text, sizeof text);
	}
	putx(cli, vtxt, etxt, NULL);
	flushw(cli);
}

/** callback of checking */
static
void
checkcb(
	void *closure,
	const data_value_t *value
) {
	testcheckcb(closure, value, true);
}

/** callback of testing */
static
void
testcb(
	void *closure,
	const data_value_t *value
) {
	testcheckcb(closure, value, false);
}


/** callback of getting list of entries */
static
void
getcb(
	void *closure,
	const data_key_t *key,
	const data_value_t *value
) {
	client_t *cli = closure;
	char text[30];

	putx(cli, _item_, key->client, key->session, key->user, key->permission,
		value->value, exp2txt(value->expire, text, sizeof text), NULL);
}

/** handle a request */
static
void
onrequest(
	client_t *cli,
	int count,
	const char *args[]
) {
	bool nextlog;
	int rc;
	data_key_t key;
	data_value_t value;

	/* just ignore empty lines */
	if (count == 0)
		return;

	/* emit the log */
	if (cyn_server_log)
		dolog(cli, 1, count, args);

	/* version hand-shake */
	if (!cli->version) {
		if (!ckarg(args[0], _cynagora_, 0) || count != 2 || !ckarg(args[1], "1", 0))
			goto invalid;
		putx(cli, _yes_, "1", cyn_changeid_string(), NULL);
		flushw(cli);
		cli->version = 1;
		return;
	}

	switch(args[0][0]) {
	case 'a': /* agent */
		if (ckarg(args[0], _agent_, 1) && count == 5) {
			if (cli->type != server_Agent)
				break;
			/* TODO */ break;
			return;
		}
		break;
	case 'c': /* check */
		if (ckarg(args[0], _check_, 1) && count == 5) {
			cli->checked = 1;
			key.client = args[1];
			key.session = args[2];
			key.user = args[3];
			key.permission = args[4];
			cyn_check_async(checkcb, cli, &key);
			return;
		}
		break;
	case 'd': /* drop */
		if (ckarg(args[0], _drop_, 1) && count == 5) {
			if (cli->type != server_Admin)
				break;
			if (!cli->entered)
				break;
			key.client = args[1];
			key.session = args[2];
			key.user = args[3];
			key.permission = args[4];
			rc = cyn_drop(&key);
			send_done_or_error(cli, rc);
			return;
		}
		break;
	case 'e': /* enter */
		if (ckarg(args[0], _enter_, 1) && count == 1) {
			if (cli->type != server_Admin)
				break;
			if (cli->entered || cli->entering)
				break;
			cli->entering = true;
			/* TODO: remove from polling until entered? */
			cyn_enter_async(entercb, cli);
			return;
		}
		break;
	case 'g': /* get */
		if (ckarg(args[0], _get_, 1) && count == 5) {
			if (cli->type != server_Admin)
				break;
			key.client = args[1];
			key.session = args[2];
			key.user = args[3];
			key.permission = args[4];
			cyn_list(getcb, cli, &key);
			send_done(cli);
			return;
		}
		break;
	case 'l': /* leave */
		if (ckarg(args[0], _leave_, 1) && count <= 2) {
			if (cli->type != server_Admin)
				break;
			if (count == 2 && !ckarg(args[1], _commit_, 0) && !ckarg(args[1], _rollback_, 0))
				break;
			if (!cli->entered)
				break;
			rc = cyn_leave(cli, count == 2 && ckarg(args[1], _commit_, 0));
			cli->entered = false;
			send_done_or_error(cli, rc);
			return;
		}
		if (ckarg(args[0], _log_, 1) && count <= 2) {
			nextlog = cyn_server_log;
			if (cli->type != server_Admin)
				break;
			if (count == 2) {
				if (!ckarg(args[1], _on_, 0) && !ckarg(args[1], _off_, 0))
					break;
				nextlog = ckarg(args[1], _on_, 0);
			}
			putx(cli, _done_, nextlog ? _on_ : _off_, NULL);
			flushw(cli);
			cyn_server_log = nextlog;
			return;
		}
		break;
	case 's': /* set */
		if (ckarg(args[0], _set_, 1) && (count == 6 || count == 7)) {
			if (cli->type != server_Admin)
				break;
			if (!cli->entered)
				break;
			if (count == 6)
				value.expire = 0;
			else
				value.expire = strtoll(args[6], NULL, 10);
			key.client = args[1];
			key.session = args[2];
			key.user = args[3];
			key.permission = args[4];
			value.value = args[5];
			rc = cyn_set(&key, &value);
			send_done_or_error(cli, rc);
			return;
		}
		break;
	case 't': /* test */
		if (ckarg(args[0], _test_, 1) && count == 5) {
			cli->checked = 1;
			key.client = args[1];
			key.session = args[2];
			key.user = args[3];
			key.permission = args[4];
			cyn_test_async(testcb, cli, &key);
			return;
		}
		break;
	}
invalid: /* invalid rest detected */
	send_error(cli, "invalid");
	if (!cli->relax)
		cli->invalid = 1;
}

/** on change callback, emits a clear for caching */
static
void
onchange(
	void *closure
) {
	client_t *cli = closure;
	if (cli->checked) {
		cli->checked = false;
		putx(cli, _clear_, cyn_changeid_string(), NULL);
		flushw(cli);
	}
}

/** destroy a client */
static
void
destroy_client(
	client_t *cli,
	bool closefds
) {
	if (closefds)
		close(cli->pollitem.fd);
	if (cli->entering)
		cyn_enter_async_cancel(entercb, cli);
	if (cli->entered)
		cyn_leave(cli, false);
	cyn_on_change_remove(onchange, cli);
	prot_destroy(cli->prot);
	free(cli);
}

/** handle client requests */
static
void
on_client_event(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
) {
	int nargs, nr;
	const char **args;
	client_t *cli = pollitem->closure;

	/* is it a hangup? */
	if (events & EPOLLHUP)
		goto terminate;

	/* possible input */
	if (events & EPOLLIN) {
		nr = prot_read(cli->prot, cli->pollitem.fd);
		if (nr <= 0)
			goto terminate;
		nargs = prot_get(cli->prot, &args);
		while (nargs >= 0) {
			onrequest(cli, nargs, args);
			if (cli->invalid && !cli->relax)
				goto terminate;
			prot_next(cli->prot);
			nargs = prot_get(cli->prot, &args);
		}
	}
	return;

	/* terminate the client session */
terminate:
	pollitem_del(&cli->pollitem, pollfd);
	destroy_client(cli, true);
}

/** create a client */
static
int
create_client(
	client_t **pcli,
	int fd,
	server_type_t type
) {
	client_t *cli;
	int rc;

	/* allocate the object */
	*pcli = cli = calloc(1, sizeof *cli);
	if (cli == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* create protocol object */
	rc = prot_create(&cli->prot);
	if (rc < 0)
		goto error2;

	/* monitor change and caching */
	rc = cyn_on_change_add(onchange, cli);
	if (rc < 0)
		goto error3;

	/* records the file descriptor */
	cli->type = type;
	cli->version = 0; /* version not set */
	cli->relax = true; /* relax on error */
	cli->invalid = false; /* not invalid */
	cli->entered = false; /* not entered */
	cli->entering = false; /* not entering */
	cli->checked = false; /* no check made */
	cli->pollitem.handler = on_client_event;
	cli->pollitem.closure = cli;
	cli->pollitem.fd = fd;
	return 0;
error3:
	prot_destroy(cli->prot);
error2:
	free(cli);
error:
	*pcli = NULL;
	return rc;
}

/** handle server events */
static
void
on_server_event(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd,
	server_type_t type
) {
	int servfd = pollitem->fd;
	int fd, rc;
	struct sockaddr saddr;
	socklen_t slen;
	client_t *cli;

	/* is it a hangup? it shouldn't! */
	if (events & EPOLLHUP) {
		fprintf(stderr, "unexpected server socket closing\n");
		exit(2);
	}

	/* EPOLLIN is the only expected event but asserting makes fear */
	if (!(events & EPOLLIN))
		return;

	/* accept the connection */
	slen = (socklen_t)sizeof saddr;
	fd = accept(servfd, &saddr, &slen);
	if (fd < 0) {
		fprintf(stderr, "can't accept connection: %m\n");
		return;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fcntl(fd, F_SETFL, O_NONBLOCK);

	/* create a client for the connection */
	rc = create_client(&cli, fd, type);
	if (rc < 0) {
		fprintf(stderr, "can't create client connection: %s\n", strerror(-rc));
		close(fd);
		return;
	}

	/* add the client to the epolling */
	rc = pollitem_add(&cli->pollitem, EPOLLIN, pollfd);
	if (rc < 0) {
		fprintf(stderr, "can't poll client connection: %s\n", strerror(-rc));
		destroy_client(cli, 1);
		return;
	}
}

/** handle check server events */
static
void
on_check_server_event(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
) {
	on_server_event(pollitem, events, pollfd, server_Check);
}

/** handle admin server events */
static
void
on_admin_server_event(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
) {
	on_server_event(pollitem, events, pollfd, server_Admin);
}

/** handle admin server events */
static
void
on_agent_server_event(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
) {
	on_server_event(pollitem, events, pollfd, server_Agent);
}

/** destroy a server */
void
cyn_server_destroy(
	cyn_server_t *server
) {
	if (server) {
		if (server->pollfd >= 0)
			close(server->pollfd);
		if (server->admin.fd >= 0)
			close(server->admin.fd);
		if (server->check.fd >= 0)
			close(server->check.fd);
		free(server);
	}
}

/** create a server */
int
cyn_server_create(
	cyn_server_t **server,
	const char *admin_socket_spec,
	const char *check_socket_spec,
	const char *agent_socket_spec
) {
	cyn_server_t *srv;
	int rc;

	/* allocate the structure */
	*server = srv = malloc(sizeof *srv);
	if (srv == NULL) {
		rc = -ENOMEM;
		fprintf(stderr, "can't alloc memory: %m\n");
		goto error;
	}

	/* create the polling fd */
	srv->admin.fd = srv->check.fd = srv->agent.fd = -1;
	srv->pollfd = epoll_create1(EPOLL_CLOEXEC);
	if (srv->pollfd < 0) {
		rc = -errno;
		fprintf(stderr, "can't create polling: %m\n");
		goto error2;
	}

	/* create the admin server socket */
	admin_socket_spec = cyn_get_socket_admin(admin_socket_spec);
	srv->admin.fd = socket_open(admin_socket_spec, 1);
	if (srv->admin.fd < 0) {
		rc = -errno;
		fprintf(stderr, "can't create admin server socket %s: %m\n", admin_socket_spec);
		goto error2;
	}

	/* add the admin server to pollfd */
	srv->admin.handler = on_admin_server_event;
	srv->admin.closure = srv;
	rc = pollitem_add(&srv->admin, EPOLLIN, srv->pollfd);
	if (rc < 0) {
		rc = -errno;
		fprintf(stderr, "can't poll admin server: %m\n");
		goto error2;
	}

	/* create the check server socket */
	check_socket_spec = cyn_get_socket_check(check_socket_spec);
	srv->check.fd = socket_open(check_socket_spec, 1);
	if (srv->check.fd < 0) {
		rc = -errno;
		fprintf(stderr, "can't create check server socket %s: %m\n", check_socket_spec);
		goto error2;
	}

	/* add the check server to pollfd */
	srv->check.handler = on_check_server_event;
	srv->check.closure = srv;
	rc = pollitem_add(&srv->check, EPOLLIN, srv->pollfd);
	if (rc < 0) {
		rc = -errno;
		fprintf(stderr, "can't poll check server: %m\n");
		goto error2;
	}

	/* create the agent server socket */
	agent_socket_spec = cyn_get_socket_agent(agent_socket_spec);
	srv->agent.fd = socket_open(agent_socket_spec, 1);
	if (srv->agent.fd < 0) {
		rc = -errno;
		fprintf(stderr, "can't create agent server socket %s: %m\n", agent_socket_spec);
		goto error2;
	}

	/* add the agent server to pollfd */
	srv->agent.handler = on_agent_server_event;
	srv->agent.closure = srv;
	rc = pollitem_add(&srv->agent, EPOLLIN, srv->pollfd);
	if (rc < 0) {
		rc = -errno;
		fprintf(stderr, "can't poll agent server: %m\n");
		goto error2;
	}

	return 0;

error2:
	if (srv->pollfd >= 0)
		close(srv->pollfd);
	if (srv->admin.fd >= 0)
		close(srv->admin.fd);
	if (srv->check.fd >= 0)
		close(srv->check.fd);
	if (srv->agent.fd >= 0)
		close(srv->agent.fd);
	free(srv);
error:
	*server = NULL;
	return rc;
}

/** stop the server */
void
cyn_server_stop(
	cyn_server_t *server,
	int status
) {
	server->stopped = status ?: INT_MIN;
}

/** create a server */
int
cyn_server_serve(
	cyn_server_t *server
) {
	/* process inputs */
	server->stopped = 0;
	while(!server->stopped) {
		pollitem_wait_dispatch(server->pollfd, -1);
	}
	return server->stopped == INT_MIN ? 0 : server->stopped;
}

