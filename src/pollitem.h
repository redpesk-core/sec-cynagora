/*
 * Copyright (C) 2018-2026 IoT.bzh Company
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
#pragma once
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF EPOLL HELPER                                             */
/******************************************************************************/
/******************************************************************************/

/** structure for using epoll easily */
typedef struct pollitem pollitem_t;

/**
 * Structure for using epoll easily
 */
struct pollitem
{
	/** callback on event */
	void (*handler)(pollitem_t *pollitem, uint32_t events, int pollfd);

	/** data of any kind free to use */
	void *closure;

	/** file descriptor */
	int fd;
};

/**
 * Add a pollitem to epoll
 *
 * @param pollitem the pollitem to add
 * @param events expected events
 * @param pollfd file descriptor of the epoll
 * @return 0 on success or -1 with errno set accordingly to epoll_ctl
 */
extern
int
pollitem_add(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
);

/**
 * Modify a pollitem of epoll
 *
 * @param pollitem the pollitem to modify
 * @param events expected events
 * @param pollfd file descriptor of the epoll
 * @return 0 on success or -1 with errno set accordingly to epoll_ctl
 */
extern
int
pollitem_mod(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
);

/**
 * Delete a pollitem from epoll
 *
 * @param pollitem the pollitem to delete
 * @param pollfd file descriptor of the epoll
 * @return 0 on success or -1 with errno set accordingly to epoll_ctl
 */
extern
int
pollitem_del(
	pollitem_t *pollitem,
	int pollfd
);

/**
 * Wait one event on epoll and dispatch it to its pollitem callback
 *
 * @param pollfd file descriptor of the epoll
 * @param timeout time to wait
 * @return 0 on timeout
 *         1 if a callback was called
 *         -1 with errno set accordingly to epoll_wait
 */
extern
int
pollitem_wait_dispatch(
	int pollfd,
	int timeout
);
