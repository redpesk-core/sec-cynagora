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

/** structure for using epoll easily */
typedef struct pollitem pollitem_t;

struct pollitem
{
	/** callback on event */
	void (*handler)(pollitem_t *pollitem, uint32_t events, int pollfd);

	/** data */
	void *closure;

	/** file */
	int fd;
};

extern
int
pollitem_add(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
);

extern
int
pollitem_mod(
	pollitem_t *pollitem,
	uint32_t events,
	int pollfd
);

extern
int
pollitem_del(
	pollitem_t *pollitem,
	int pollfd
);

extern
int
pollitem_wait_dispatch(
	int pollfd,
	int timeout
);
