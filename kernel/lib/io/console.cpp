// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/io.h>

#include <arch/ops.h>
#include <assert.h>
#include <ctype.h>
#include <debug.h>
#include <err.h>
#include <kernel/auto_lock.h>
#include <kernel/thread.h>
#include <lib/debuglog.h>
#include <list.h>
#include <platform.h>
#include <platform/debug.h>
#include <string.h>
#include <vm/vm.h>

/* enable this to cause the kernel-originated messages to splat messages out of the platform
 * putc mechanism immediately instead of going through the debug log
 */
#ifndef ENABLE_KERNEL_LL_DEBUG
#define ENABLE_KERNEL_LL_DEBUG 0
#endif

/* routines for dealing with main console io */

static SpinLock dputc_spin_lock;

void __kernel_serial_write(const char* str, size_t len) {
    AutoSpinLock guard(&dputc_spin_lock);

    /* write out the serial port */
    platform_dputs_irq(str, len);
}

static SpinLock print_spin_lock;
static struct list_node print_callbacks = LIST_INITIAL_VALUE(print_callbacks);

void __kernel_console_write(const char* str, size_t len) {
    print_callback_t* cb;

    /* print to any registered loggers */
    if (!list_is_empty(&print_callbacks)) {
        AutoSpinLock guard(&print_spin_lock);

        list_for_every_entry (&print_callbacks, cb, print_callback_t, entry) {
            if (cb->print)
                cb->print(cb, str, len);
        }
    }
}

static void __kernel_stdout_write(const char* str, size_t len) {
#if WITH_LIB_DEBUGLOG && !ENABLE_KERNEL_LL_DEBUG
    if (dlog_write(0, str, len)) {
        __kernel_console_write(str, len);
        __kernel_serial_write(str, len);
    }
#else
    __kernel_console_write(str, len);
    __kernel_serial_write(str, len);
#endif
}

#if WITH_DEBUG_LINEBUFFER
static void __kernel_stdout_write_buffered(const char* str, size_t len) {
    thread_t* t = get_current_thread();

    if (unlikely(t == NULL)) {
        __kernel_stdout_write(str, len);
        return;
    }

    char* buf = t->linebuffer;
    size_t pos = t->linebuffer_pos;

    // look for corruption and don't continue
    if (unlikely(!is_kernel_address((uintptr_t)buf) || pos >= THREAD_LINEBUFFER_LENGTH)) {
        const char* str = "<linebuffer corruption>\n";
        __kernel_stdout_write(str, strlen(str));
        return;
    }

    while (len-- > 0) {
        char c = *str++;
        buf[pos++] = c;
        if (c == '\n') {
            __kernel_stdout_write(buf, pos);
            pos = 0;
            continue;
        }
        if (pos == (THREAD_LINEBUFFER_LENGTH - 1)) {
            buf[pos++] = '\n';
            __kernel_stdout_write(buf, pos);
            pos = 0;
            continue;
        }
    }
    t->linebuffer_pos = pos;
}
#endif

void register_print_callback(print_callback_t* cb) {
    AutoSpinLock guard(&print_spin_lock);

    list_add_head(&print_callbacks, &cb->entry);
}

void unregister_print_callback(print_callback_t* cb) {
    AutoSpinLock guard(&print_spin_lock);

    list_delete(&cb->entry);
}

int __printf_output_func(const char* s, size_t len, void* state) {
#if WITH_DEBUG_LINEBUFFER
    __kernel_stdout_write_buffered(s, len);
#else
    __kernel_stdout_write(s, len);
#endif
    return static_cast<int>(len);
}
