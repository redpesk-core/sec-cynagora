/*
 * Copyright (C) 2018-2023 IoT.bzh Company
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
/* IMPLEMENTATION OF QUEUE OF DATABASE MODIFIERS                              */
/******************************************************************************/
/******************************************************************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "data.h"
#include "db.h"

#define DROP 0
#define SET  1

/**
 * Queue
 */
struct queue
{
	/** read index within queue */
	uint32_t read;

	/** write index within queue */
	uint32_t write;

	/** capacity of the queue */
	uint32_t capacity;

	/** the memory of the queue */
	void *queue;
};
typedef struct queue queue_t;

/** the queue */
static queue_t queue;

/**
 * Read data from the queue
 *
 * @param data where to store the data
 * @param length length of the data to read
 * @return true on success or false on error
 */
static
bool
qread(
	void *data,
	uint32_t length
) {
	if (queue.read + length > queue.write)
		return false;

	memcpy(data, queue.queue + queue.read, length);
	queue.read += length;
	return true;
}

/**
 * Get a time from the queue
 *
 * @param value where to store the time
 * @return true on success or false on error
 */
static
bool
qget_time(
	time_t *value
) {
	return qread(value, sizeof *value);
}

/**
 * Get a string from the queue
 *
 * @param text where to store the string
 * @return true on success or false on error
 */
static
bool
qget_string(
	const char **text
) {
	char *p = queue.queue + queue.read;
	uint32_t length = (uint32_t)strlen(p);
	if (queue.read + length >= queue.write)
		return false;
	*text = p;
	queue.read += length + 1;
	return true;
}

/**
 * Write the data to the queue
 *
 * @param data data to write
 * @param length length of the data
 * @return true on success or false on error
 */
static
bool
qwrite(
	const void *data,
	uint32_t length
) {
	uint32_t c;
	void *b;

	c = queue.capacity;
	while (c < queue.write + length)
		c += 4096;
	if (c != queue.capacity) {
		b = realloc(queue.queue, c);
		if (b == NULL)
			return false;
		queue.queue = b;
		queue.capacity = c;
	}
	memcpy(queue.queue + queue.write, data, length);
	queue.write += length;
	return true;
}

/**
 * Put the time in the queue
 *
 * @param value the value to put
 * @return true on success or false on error
 */
static
bool
qput_time(
	time_t value
) {
	return qwrite(&value, sizeof value);
}

/**
 * Put the text in the queue
 *
 * @param text the text to put
 *
 * @return true on success or false on error
 */
static
bool
qput_string(
	const char *text
) {
	size_t len;
	text = text ?: "";

	/* check length */
	len = strnlen(text, MAX_NAME_LENGTH + 1);
	if (len > MAX_NAME_LENGTH)
		return false;

	return qwrite(text, 1 + (uint32_t)len);
}

/* see queue.h */
int
queue_drop(
	const data_key_t *key
) {
	return qput_string(key->client)
		&& qput_string(key->session)
		&& qput_string(key->user)
		&& qput_string(key->permission)
		&& qput_string(NULL)
			? 0 : -(errno = ENOMEM);
}

/* see queue.h */
int
queue_set(
	const data_key_t *key,
	const data_value_t *value
) {
	return qput_string(key->client)
		&& qput_string(key->session)
		&& qput_string(key->user)
		&& qput_string(key->permission)
		&& qput_string(value->value)
		&& qput_time(value->expire)
			? 0 : -(errno = ENOMEM);
}

/* see queue.h */
void
queue_clear(
) {
	queue.write = 0;
}

/* see queue.h */
int
queue_play(
) {
	int rc, rc2;
	data_key_t key;
	data_value_t value;

	rc = 0;
	queue.read = 0;
	while (queue.read < queue.write) {
		rc2 = -EINVAL;
		if (qget_string(&key.client)
		 && qget_string(&key.session)
		 && qget_string(&key.user)
		 && qget_string(&key.permission)
		 && qget_string(&value.value)) {
			if (!value.value[0])
				rc2 = db_drop(&key);
			else {
				if (qget_time(&value.expire))
					rc2 = db_set(&key, &value);
			}
		}
		if (rc2 != 0 && rc == 0)
			rc = rc2;
	}
	return rc;
}

