/*
 * Copyright (C) 2024-2026 IoT.bzh Company
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

#include "fbuf.h"

/**
 * Read in 'fb' the file of 'name'
 * @param fb the fbuf
 * @param name the name of the file to read
 * @return 0 on success
 *         -EFBIG if the file is too big
 *         -errno system error
 */
extern int fbuf_sysfile_read_file(fbuf_t *fb, const char *name);

/**
 * Write to file of 'name' content of 'fb'
 * @param fb the fbuf
 * @param name the name of the file to write
 * @return 0 on success
 *         -EFBIG if the file is too big
 *         -errno system error
 */
extern int fbuf_sysfile_write_file(fbuf_t *fb, const char *name);

/**
 * Read in 'fb' from its main storage
 * @param fb the fbuf
 * @return 0 on success
 *         -EFBIG if the file is too big
 *         -errno system error
 */
extern int fbuf_sysfile_read(fbuf_t *fb);

/**
 * Sync
 * @param fb the fbuf
 * @return 0 on success
 *         -EFBIG if the file is too big
 *         -errno system error
 */
extern int fbuf_sysfile_sync(fbuf_t *fb);

/**
 * Create the back-up file
 * Backup is managed using hard links. It implies that the operating system
 * handles hard links.
 * @param fb the fbuf
 * @return 0 in case of success
 *         a negative -errno code
 */
extern int fbuf_sysfile_backup(fbuf_t *fb);

/**
 * recover data from latest backup
 * @param fb the fbuf
 * @return 0 on success
 *         a negative -errno code
 */
extern int fbuf_sysfile_recover(fbuf_t *fb);
