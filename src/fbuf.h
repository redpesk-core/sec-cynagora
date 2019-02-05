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

#pragma once

/**
 * A fbuf records file data and access
 */
struct fbuf
{
        /** filename */
        char *name;

        /** backup filename */
        char *backup;

        /** in memory copy of the file */
        void *buffer;

        /** size currently allocated */
        uint32_t capacity;

        /** size of the file */
        uint32_t size;

        /** size saved to the file */
        uint32_t saved;

        /** size currently used */
        uint32_t used;
};

/** short type */
typedef struct fbuf fbuf_t;


/** open in 'fb' the file of 'name' and optionnal 'backup' name */
extern
int
fbuf_open(
	fbuf_t	*fb,
	const char *name,
	const char *backup
);

/** close the file 'fb' */
extern
void
fbuf_close(
	fbuf_t	*fb
);

/** write to file 'fb' the unsaved bytes and flush the content to the file */
extern
int
fbuf_sync(
	fbuf_t	*fb
);

/** allocate enough memory in 'fb' to store 'count' bytes */
extern
int
fbuf_ensure_capacity(
	fbuf_t	*fb,
	uint32_t count
);

/** put at 'offset' in the memory of 'fb' the 'count' bytes pointed by 'buffer' */
extern
int
fbuf_put(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count,
	uint32_t offset
);

/** append at end in the memory of 'fb' the 'count' bytes pointed by 'buffer' */
extern
int
fbuf_append(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count
);

/** check or make identification of file 'fb' by 'id' of 'len' */
extern
int
fbuf_identify(
	fbuf_t	*fb,
	const char *id,
	uint32_t idlen
);

/** check or make identification by 'uuid' of file 'fb' */
extern
int
fbuf_open_identify(
	fbuf_t	*fb,
	const char *name,
	const char *backup,
	const char *id,
	uint32_t idlen
);

extern
int
fbuf_backup(
	fbuf_t	*fb
);

extern
int
fbuf_recover(
	fbuf_t	*fb
);

