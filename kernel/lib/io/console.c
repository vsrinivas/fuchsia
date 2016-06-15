// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/io.h>

#include <err.h>
#include <ctype.h>
#include <debug.h>
#include <assert.h>
#include <list.h>
#include <string.h>
#include <arch/ops.h>
#include <platform.h>
#include <platform/debug.h>
#include <kernel/thread.h>

#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif

/* routines for dealing with main console io */

#if WITH_LIB_SM
#define PRINT_LOCK_FLAGS SPIN_LOCK_FLAG_IRQ_FIQ
#else
#define PRINT_LOCK_FLAGS SPIN_LOCK_FLAG_INTERRUPTS
#endif

static spin_lock_t print_spin_lock = 0;
static struct list_node print_callbacks = LIST_INITIAL_VALUE(print_callbacks);


#if WITH_DEBUG_LINEBUFFER
static void __out_count(const char *str, size_t len);

static void out_count(const char *str, size_t len) {
    thread_t *t = get_current_thread();

    if (t == NULL) {
        __out_count(str, len);
        return;
    }

    char *buf = t->linebuffer;
    size_t pos = t->linebuffer_pos;

    while (len-- > 0) {
        char c = *str++;
        buf[pos++] = c;
        if (c == '\n') {
            __out_count(buf, pos);
            pos = 0;
            continue;
        }
        if (pos == (THREAD_LINEBUFFER_LENGTH - 1)) {
            buf[pos++] = '\n';
            __out_count(buf, pos);
            pos = 0;
            continue;
        }
    }
    t->linebuffer_pos = pos;
}
#else
#define __out_count out_count
#endif

static spin_lock_t dputc_spin_lock = 0;
static void __raw_out_count(const char *str, size_t len) {
    spin_lock_saved_state_t state;
    spin_lock_save(&dputc_spin_lock, &state, PRINT_LOCK_FLAGS);
    /* write out the serial port */
    for (size_t i = 0; i < len; i++) {
        platform_dputc(str[i]);
    }
    spin_unlock_restore(&dputc_spin_lock, state, PRINT_LOCK_FLAGS);
}

/* print lock must be held when invoking out, outs, outc */
static void __out_count(const char *str, size_t len)
{
    print_callback_t *cb;

    /* print to any registered loggers */
    if (!list_is_empty(&print_callbacks)) {
        spin_lock_saved_state_t state;
        spin_lock_save(&print_spin_lock, &state, PRINT_LOCK_FLAGS);

        list_for_every_entry(&print_callbacks, cb, print_callback_t, entry) {
            if (cb->print)
                cb->print(cb, str, len);
        }

        spin_unlock_restore(&print_spin_lock, state, PRINT_LOCK_FLAGS);
    }

#if WITH_LIB_DEBUGLOG
    if (dlog_write(DLOG_FLAG_KERNEL, str, len)) {
        __raw_out_count(str, len);
    }
#else
    __raw_out_count(str, len);
#endif
}

void register_print_callback(print_callback_t *cb)
{
    spin_lock_saved_state_t state;
    spin_lock_save(&print_spin_lock, &state, PRINT_LOCK_FLAGS);

    list_add_head(&print_callbacks, &cb->entry);

    spin_unlock_restore(&print_spin_lock, state, PRINT_LOCK_FLAGS);
}

void unregister_print_callback(print_callback_t *cb)
{
    spin_lock_saved_state_t state;
    spin_lock_save(&print_spin_lock, &state, PRINT_LOCK_FLAGS);

    list_delete(&cb->entry);

    spin_unlock_restore(&print_spin_lock, state, PRINT_LOCK_FLAGS);
}

static ssize_t __debug_stdio_write(io_handle_t *io, const char *s, size_t len)
{
    out_count(s, len);
    return len;
}

static ssize_t __debug_stdio_read(io_handle_t *io, char *s, size_t len)
{
    if (len == 0)
        return 0;

    int err = platform_dgetc(s, true);
    if (err < 0)
        return err;

    return 1;
}

/* global console io handle */
static const io_handle_hooks_t console_io_hooks = {
    .write  = __debug_stdio_write,
    .read   = __debug_stdio_read,
};

io_handle_t console_io = IO_HANDLE_INITIAL_VALUE(&console_io_hooks);
