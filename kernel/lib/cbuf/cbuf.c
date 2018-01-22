// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdlib.h>
#include <debug.h>
#include <trace.h>
#include <pow2.h>
#include <string.h>
#include <assert.h>
#include <lib/cbuf.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>

#define LOCAL_TRACE 0

#define INC_POINTER(cbuf, ptr, inc) \
    modpow2(((ptr) + (inc)), (cbuf)->len_pow2)

void cbuf_initialize(cbuf_t *cbuf, size_t len)
{
    cbuf_initialize_etc(cbuf, len, malloc(len));
}

void cbuf_initialize_etc(cbuf_t *cbuf, size_t len, void *buf)
{
    DEBUG_ASSERT(cbuf);
    DEBUG_ASSERT(len > 0);
    DEBUG_ASSERT(ispow2(len));

    cbuf->head = 0;
    cbuf->tail = 0;
    cbuf->len_pow2 = log2_uint_floor(len);
    cbuf->buf = buf;
    event_init(&cbuf->event, false, 0);
    spin_lock_init(&cbuf->lock);

    LTRACEF("len %zu, len_pow2 %u\n", len, cbuf->len_pow2);
}

size_t cbuf_space_avail(cbuf_t *cbuf)
{
    uint consumed = modpow2((uint)(cbuf->head - cbuf->tail), cbuf->len_pow2);
    return valpow2(cbuf->len_pow2) - consumed - 1;
}

size_t cbuf_write_char(cbuf_t *cbuf, char c)
{
    DEBUG_ASSERT(cbuf);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    size_t ret = 0;
    if (cbuf_space_avail(cbuf) > 0) {
        cbuf->buf[cbuf->head] = c;

        cbuf->head = INC_POINTER(cbuf, cbuf->head, 1);
        ret = 1;

        if (cbuf->head != cbuf->tail)
            event_signal(&cbuf->event, true);
    }

    spin_unlock_irqrestore(&cbuf->lock, state);

    return ret;
}

size_t cbuf_read_char(cbuf_t *cbuf, char *c, bool block)
{
    DEBUG_ASSERT(cbuf);
    DEBUG_ASSERT(c);

retry:
    if (block)
        event_wait(&cbuf->event);

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&cbuf->lock, state);

    // see if there's data available
    size_t ret = 0;
    if (cbuf->tail != cbuf->head) {

        *c = cbuf->buf[cbuf->tail];
        cbuf->tail = INC_POINTER(cbuf, cbuf->tail, 1);

        if (cbuf->tail == cbuf->head) {
            // we've emptied the buffer, unsignal the event
            event_unsignal(&cbuf->event);
        }

        ret = 1;
    }

    spin_unlock_irqrestore(&cbuf->lock, state);

    if (block && ret == 0)
        goto retry;

    return ret;
}

