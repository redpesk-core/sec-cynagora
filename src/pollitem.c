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
/* IMPLEMENTATION OF EPOLL HELPER                                             */
/******************************************************************************/
/******************************************************************************/

#include <stdint.h>
#include <sys/epoll.h>

#include "pollitem.h"

/**
 * Wraps the call to epoll_ctl for operation 'op'
 *
 * @param pollitem the pollitem to process
 * @param events the expected events
 * @param pollfd the file descriptor for epoll
 * @param op the operation to perform
 * @return 0 on success or -1 with errno set accordingly to epoll_ctl
 */
static
int
pollitem_do(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd,
	int op
) {
	struct epoll_event ev = { .events = events, .data.ptr = pollitem };
	return epoll_ctl(pollfd, op, pollitem->fd, &ev);
}

/* see pollitem.h */
int
pollitem_add(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
) {
	return pollitem_do(pollitem, events, pollfd, EPOLL_CTL_ADD);
}

/* see pollitem.h */
int
pollitem_mod(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
) {
	return pollitem_do(pollitem, events, pollfd, EPOLL_CTL_MOD);
}

/* see pollitem.h */
int
pollitem_del(
	pollitem_t *pollitem,
	int pollfd
) {
	return pollitem_do(pollitem, 0, pollfd, EPOLL_CTL_DEL);
}

/* see pollitem.h */
int
pollitem_wait_dispatch(
	int pollfd,
	int timeout
) {
	int rc;
	struct epoll_event ev;
	pollitem_t *pi;

	rc = epoll_wait(pollfd, &ev, 1, timeout);
	if (rc == 1) {
		pi = ev.data.ptr;
		pi->handler(pi, ev.events, pollfd);
	}
	return rc;
}
