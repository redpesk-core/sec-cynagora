/*
 * Copyright (C) 2018-2021 IoT.bzh Company
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
/* IMPLEMENTATION OF BUFFERED FILES                                           */
/******************************************************************************/
/******************************************************************************/

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


/**
 * open in 'fb' the file of 'name'
 * @param fb the fbuf
 * @param name name of the filename to read
 * @param backup name of the backup to use (can be NULL)
 * @return 0 on success
 *         a negative -errno code
 */
extern
int
fbuf_open(
	fbuf_t	*fb,
	const char *name,
	const char *backup
);

/**
 * close the fbuf 'fb'
 * @param fb the fbuf to close
 */
extern
void
fbuf_close(
	fbuf_t	*fb
);

/**
 * write to fbuf 'fb' the unsaved bytes and flush the content to the file
 * @param fb the fbuf
 * @return 0 on success
 *         a negative -errno code
 */
extern
int
fbuf_sync(
	fbuf_t	*fb
);

/**
 * allocate enough memory in 'fb' to store 'count' bytes
 * @param fb the fbuf
 * @param capacity expected capacity
 * @return 0 on success
 *         -ENOMEM if out of memory
 */
extern
int
fbuf_ensure_capacity(
	fbuf_t	*fb,
	uint32_t count
);

/**
 * put at 'offset' in the memory of 'fb' the 'count' bytes pointed by 'buffer'
 * @param fb the fbuf
 * @param buffer pointer to the data
 * @param count size of data MUST BE GREATER THAN ZERO
 * @param offset where to put the data
 * @return 0 on success
 *         -ENOMEM if out of memory
 */
extern
int
fbuf_put(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count,
	uint32_t offset
);

/**
 * append at end in the memory of 'fb' the 'count' bytes pointed by 'buffer'
 * @param fb the fbuf
 * @param buffer pointer to the data
 * @param count size of data MUST BE GREATER THAN ZERO
 * @return 0 on success
 *         -ENOMEM if out of memory
 */
extern
int
fbuf_append(
	fbuf_t	*fb,
	const void *buffer,
	uint32_t count
);

/**
 * Check or make identification of file 'fb' by 'id' of 'idlen'
 * If the content is empty, it initialize the identification prefix.
 * Otherwise, not empty, the check is performed.
 * @param fb the fbuf to check
 * @param id the prefix identifier to check
 * @param idlen the length of the identifier
 * @return 0 on success
 *         -ENOKEY if identification failed
 */
extern
int
fbuf_identify(
	fbuf_t	*fb,
	const char *id,
	uint32_t idlen
);

/**
 * Open the fbuf 'fb' of 'name', 'backup' and check that it has the
 * prefix identifier 'id' of length 'idlen'.
 * @param fb the fbuf to open
 * @param name file name to open
 * @param backup name of the backup file
 * @param id identifier prefix value
 * @param idlen length of the identifier prefix
 * @return 0 in case of success
 *         -ENOMEM if out of memory
 *         -ENOKEY if identification failed
 *         a negative -errno code
 */
extern
int
fbuf_open_identify(
	fbuf_t	*fb,
	const char *name,
	const char *backup,
	const char *id,
	uint32_t idlen
);

/**
 * Create the back-up file
 * Backup is managed using hard links. It implies that the operating system
 * handles hard links.
 * @param fb the fbuf
 * @return 0 in case of success
 *         a negative -errno code
 */
extern
int
fbuf_backup(
	fbuf_t	*fb
);

/**
 * recover data from latest backup
 * @param fb the fbuf
 * @return 0 on success
 *         a negative -errno code
 */
extern
int
fbuf_recover(
	fbuf_t	*fb
);

