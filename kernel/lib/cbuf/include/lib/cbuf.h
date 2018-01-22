// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009-2013 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <sys/types.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <iovec.h>

__BEGIN_CDECLS

typedef struct cbuf {
    uint head;
    uint tail;
    uint len_pow2;
    char *buf;
    event_t event;
    spin_lock_t lock;
} cbuf_t;

/**
 * cbuf_initialize
 *
 * Initialize a cbuf structure, mallocing the underlying data buffer in the
 * process.  Make sure that the buffer has enough space for at least len bytes.
 *
 * @param[in] cbuf A pointer to the cbuf structure to allocate.
 * @param[in] len The minimum number of bytes for the underlying data buffer.
 */
void cbuf_initialize(cbuf_t *cbuf, size_t len);

/**
 * cbuf_initalize_etc
 *
 * Initialize a cbuf structure using the supplied buffer for internal storage.
 *
 * @param[in] cbuf A pointer to the cbuf structure to allocate.
 * @param[in] len The size of the supplied buffer, in bytes.
 * @param[in] buf A pointer to the memory to be used for internal storage.
 */
void cbuf_initialize_etc(cbuf_t *cbuf, size_t len, void *buf);

/**
 * cbuf_space_avail
 *
 * @param[in] cbuf The cbuf instance to query
 *
 * @return The number of free space available in the cbuf (IOW - the maximum
 * number of bytes which can currently be written)
 */
size_t cbuf_space_avail(cbuf_t *cbuf);

/* special cases for dealing with a single char of data */
size_t cbuf_read_char(cbuf_t *cbuf, char *c, bool block);
size_t cbuf_write_char(cbuf_t *cbuf, char c);

__END_CDECLS

