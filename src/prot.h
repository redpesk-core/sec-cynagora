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
/******************************************************************************/
/******************************************************************************/
/* IMPLEMENTATION OF THE PROTOCOL                                             */
/******************************************************************************/
/******************************************************************************/

typedef struct prot prot_t;

/**
 * Create the prot handler in 'prot'
 *
 * @param prot where to store the protocol handler
 * @return 0 in case of success or -ENOMEM in case of error
 */
extern
int
prot_create(
	prot_t **prot
);

/**
 * Destroys the protocol handler 'prot'
 *
 * @param prot the protocol handler
 */
/**
 *
 */
extern
void
prot_destroy(
	prot_t *prot
);

/**
 * Reset the protocol handler 'prot'
 *
 * @param prot the protocol handler
 */
extern
void
prot_reset(
	prot_t *prot
);

/**
 * Cancel any previous put not terminated with prot_put_end
 *
 * @param prot the protocol handler
 */
extern
void
prot_put_cancel(
	prot_t *prot
);

/**
 * Terminate a protocol record
 *
 * @param prot the protocol handler
 * @return 0 on success
 *         -ECANCELED if the send buffer is full
 */
extern
int
prot_put_end(
	prot_t *prot
);

/**
 * Add a field to a protocol record
 *
 * @param prot the protocol handler
 * @param field the field to add
 * @return 0 on success
 *         -ECANCELED if the send buffer is full
 */
extern
int
prot_put_field(
	prot_t *prot,
	const char *field
);

/**
 * Add a set of fields to a protocol record
 *
 * @param prot the protocol handler
 * @param count count of fields
 * @param fields array of the fields to add
 * @return 0 on success
 *         -ECANCELED if the send buffer is full
 */
extern
int
prot_put_fields(
	prot_t *prot,
	unsigned count,
	const char **fields
);

/**
 * Add a set of fields to the record of protocol and terminate it
 *
 * @param prot the protocol handler
 * @param count count of fields
 * @param fields array of the fields to add
 * @return 0 on success
 *         -ECANCELED if the send buffer is full
 */
extern
int
prot_put(
	prot_t *prot,
	unsigned count,
	const char **fields
);

/**
 * Add a variable length of items in protocol and terminate it
 *
 * @param prot the protocol handler
 * @param ... a NULL terminated list of strings
 * @return 0 on success
 *         -ECANCELED if the send buffer is full
 */
extern
int
prot_putx(
	prot_t *prot,
	...
);

/**
 * Check whether write should be done or not
 *
 * @param prot the protocol handler
 * @return 1 if there is something to write or 0 otherwise
 */
extern
int
prot_should_write(
	prot_t *prot
);

/**
 * Write the content to write and return either the count
 * of bytes written or an error code (negative). Note that
 * the returned value tries to be the same as those returned
 * by "man 2 write". The only exception is -ENODATA that is
 * returned if there is nothing to be written.
 *
 * @param prot the protocol handler
 * @param fdout the file to write
 * @return the count of bytes written or a negative -errno error code
 */
extern
int
prot_write(
	prot_t *prot,
	int fdout
);

/**
 * Is there space to receive data
 *
 * @param prot the protocol handler
 * @return 0 if there is no space or 1 if read can be called
 */
extern
int
prot_can_read(
	prot_t *prot
);

/**
 * Read data from the input file fdin
 *
 * @param prot the protocol handler
 * @param fdin the file to read
 * @return the count of bytes read or a negative -errno error code
 */
extern
int
prot_read(
	prot_t *prot,
	int fdin
);

/**
 * Get the currently received fields and its count
 *
 * @param prot the protocol handler
 * @param fields where to store the array of received fields (can be NULL)
 * @return the count of fields or -EAGAIN if no field is available
 */
extern
int
prot_get(
	prot_t *prot,
	const char ***fields
);

/**
 * Forgive the current received fields so that the next call to prot_get
 * returns the next received fields
 *
 * @param prot the protocol handler
 */
extern
void
prot_next(
	prot_t *prot
);


