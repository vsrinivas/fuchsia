/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "tracepoint.h"

#include "linuxisms.h"

#ifndef __CHECKER__
#define CREATE_TRACE_POINTS
#include "debug.h"

void __brcmf_err(const char* func, const char* fmt, ...) {
    char msg[512]; // Same value hard-coded throughout devhost.c
    va_list args;

    va_start(args, fmt);
    int n_printed = vsnprintf(msg, 512, fmt, args);
    va_end(args);
    if (n_printed < 0) {
        snprintf(msg, 512, "(Formatting error from string '%s')", fmt);
    } else if (msg[n_printed] == '\n') {
        msg[n_printed--] = 0;
    }
    zxlogf(ERROR, "brcmfmac ERROR(%s): '%s'\n", func, msg);
}

#endif
