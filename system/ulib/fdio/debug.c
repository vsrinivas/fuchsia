// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "private.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#ifdef FDIO_LLDEBUG
#define LOGBUF_MAX (ZX_LOG_RECORD_MAX - sizeof(zx_log_record_t))

static ssize_t fdio_lldebug_log_write(const void* _data, size_t len) {
    static thread_local struct {
        zx_handle_t log;
        uint32_t next;
        char data[LOGBUF_MAX];
    }* logbuf = NULL;

    if (logbuf == NULL) {
        if ((logbuf = calloc(1, sizeof(*logbuf))) == NULL) {
            return len;
        }
        if (zx_debuglog_create(0, 0, &logbuf->log) != ZX_OK) {
            free(logbuf);
            logbuf = NULL;
            return len;
        }
    }

    const char* data = _data;
    size_t r = len;
    while (len-- > 0) {
        char c = *data++;
        if (c == '\n') {
            zx_log_write(logbuf->log, logbuf->next, logbuf->data, 0);
            logbuf->next = 0;
            continue;
        }
        if (c < ' ') {
            continue;
        }
        logbuf->data[logbuf->next++] = c;
        if (logbuf->next == LOGBUF_MAX) {
            zx_log_write(logbuf->log, logbuf->next, logbuf->data, 0);
            logbuf->next = 0;
            continue;
        }
    }
    return r;
}

static unsigned debug_level = FDIO_LLDEBUG;

void fdio_lldebug_printf(unsigned level, const char* fmt, ...) {
    if (debug_level >= level) {
        va_list ap;
        char buf[128];
        va_start(ap, fmt);
        size_t sz = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        fdio_lldebug_log_write(buf, sz > sizeof(buf) ? sizeof(buf) : sz);
    }
}
#endif

void fdio_set_debug_level(unsigned level) {
#ifdef FDIO_LLDEBUG
    debug_level = level;
#endif
}
