// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#pragma GCC visibility push(hidden)

#include <assert.h>
#include <magenta/syscalls.h>
#include <stdarg.h>
#include <string.h>

#pragma GCC visibility pop

#define LOG_PREFIX "userboot: "
#define LOG_WRITE_FAIL \
    (LOG_PREFIX "Error printing error message.  No error message printed.\n")

void print(mx_handle_t log, const char* s, ...) {
    char buffer[MX_LOG_RECORD_MAX - sizeof(mx_log_record_t)];
    static_assert(sizeof(LOG_PREFIX) < sizeof(buffer), "buffer too small");

    memcpy(buffer, LOG_PREFIX, sizeof(LOG_PREFIX) - 1);
    char* p = &buffer[sizeof(LOG_PREFIX) - 1];

    va_list ap;
    va_start(ap, s);
    do {
        size_t len = strlen(s);
        if ((size_t)(&buffer[sizeof(buffer)] - p) <= len)
            __builtin_trap();
        memcpy(p, s, len);
        p += len;
        s = va_arg(ap, const char*);
    } while (s != NULL);
    va_end(ap);

    *p = '\0';

    if (log == MX_HANDLE_INVALID) {
        mx_debug_write(buffer, p - buffer);
    } else {
        mx_status_t status = mx_log_write(log, p - buffer, buffer, 0);
        if (status != NO_ERROR)
            mx_debug_write(LOG_WRITE_FAIL, strlen(LOG_WRITE_FAIL));
    }
}

void fail(mx_handle_t log, mx_status_t status, const char* msg) {
    print(log, msg, NULL);
    mx_exit(status);
}
