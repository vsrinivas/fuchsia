// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#pragma GCC visibility push(hidden)

#include <assert.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <stdarg.h>
#include <string.h>

#pragma GCC visibility pop

#define LOG_PREFIX "userboot: "
#define LOG_WRITE_FAIL \
    (LOG_PREFIX "Error printing error message.  No error message printed.\n")

static char* hexstring(char* s, size_t len, uint64_t n) {
    char tmp[16];
    char* hex = tmp;
    do {
        *hex++ = "0123456789abcdef"[n & 15];
        n >>= 4;
    } while (n);

    if (len > 2) {
        *s++ = '0';
        *s++ = 'x';
        len -= 2;
    }

    while ((hex > tmp) && (len > 0)) {
        *s++ = *--hex;
        len--;
    }
    return s;
}

static char* u64string(char* s, size_t len, uint64_t n) {
    char tmp[21]; // strlen(maxint64) + 1
    char* dec = tmp;
    do {
        *dec++ = "0123456789"[n % 10];
        n /= 10;
    } while (n > 0);

    while ((dec > tmp) && (len > 0)) {
        *s++ = *--dec;
        len--;
    }
    return s;
}

static char* i64string(char* s, size_t len, int64_t n) {
    if (n < 0) {
        // this will never be called with len < 1 so we
        // don't need to check for len
        n = -n;
        *s++ = '-';
        len--;
    }
    return u64string(s, len, n);
}

void vprintl(zx_handle_t log, const char* fmt, va_list ap) {
    char buffer[ZX_LOG_RECORD_MAX - sizeof(zx_log_record_t)];
    static_assert(sizeof(LOG_PREFIX) < sizeof(buffer), "buffer too small");

    memcpy(buffer, LOG_PREFIX, sizeof(LOG_PREFIX) - 1);
    char* p = &buffer[sizeof(LOG_PREFIX) - 1];

    size_t avail;
    const char* s;
    uint64_t n;
    int64_t i;

    while (*fmt && (avail = (size_t)(&buffer[sizeof(buffer)] - p))) {
        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        switch (*++fmt) {
        case 's':
            s = va_arg(ap, const char*);
            while (avail && *s) {
                *p++ = *s++;
                avail--;
            }
            break;
        case '.':
            fmt++;
            if (*fmt != '*')
                goto bad_format;
            fmt++;
            if (*fmt != 's')
                goto bad_format;
            i = va_arg(ap, int);
            s = va_arg(ap, const char*);
            while (avail && *s && i != 0) {
                *p++ = *s++;
                avail--;
                if (i > 0)
                    --i;
            }
            break;
        case 'z':
            fmt++;
            switch (*fmt) {
            case 'u':
                n = va_arg(ap, uint64_t);
                goto u64print;
            case 'd':
                i = va_arg(ap, int64_t);
                goto i64print;
            case 'x':
                n = va_arg(ap, uint64_t);
                goto x64print;
            default:
                goto bad_format;
            }
            break;
        case 'p':
            n = va_arg(ap, uint64_t);
            goto x64print;
        case 'u':
            n = va_arg(ap, uint32_t);
u64print:
            p = u64string(p, avail, n);
            break;
        case 'd':
            i = va_arg(ap, int32_t);
i64print:
            p = i64string(p, avail, i);
            break;
        case 'x':
            n = va_arg(ap, uint32_t);
x64print:
            p = hexstring(p, avail, n);
            break;
        default:
bad_format:
            printl(log, "printl: invalid fmt char 0x%x", *fmt);
            zx_process_exit(-1);
        }
        fmt++;
    }

    if ((log == ZX_HANDLE_INVALID) ||
        (zx_log_write(log, p - buffer, buffer, 0) != ZX_OK)) {
        zx_debug_write(buffer, p - buffer);
        zx_debug_write("\n", 1);
    }
}

void printl(zx_handle_t log, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintl(log, fmt, ap);
    va_end(ap);
}

void fail(zx_handle_t log, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintl(log, fmt, ap);
    va_end(ap);
    zx_process_exit(-1);
}
