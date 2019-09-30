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
/* IMPLEMENTATION OF QUEUE OF DATABASE MODIFIERS                              */
/******************************************************************************/
/******************************************************************************/

/**
 * Queue droping the key
 *
 * @param key the key for dropping
 * @return 0 on success or -ENOMEM on memory depletion
 */
extern
int
queue_drop(
	const data_key_t *key
);

/**
 * Queue setting of the key with the value
 *
 * @param key the key to set
 * @param value the value to set to the key
 * @return 0 on success or -ENOMEM on memory depletion
 */
extern
int
queue_set(
	const data_key_t *key,
	const data_value_t *value
);

/**
 * Clear the content of the queue
 */
extern
void
queue_clear(
);

/**
 * Play the content of the queue to alter the database accordingly to what
 * is recorded
 *
 * @return 0 in case of success or a negative error code like -errno
 */
extern
int
queue_play(
);
