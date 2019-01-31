// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <printf.h>
#include <xefi.h>

#define PCBUFMAX 126
// buffer is two larger to leave room for a \0 and room
// for a \r that may be added after a \n

typedef struct {
    int off;
    char16_t buf[PCBUFMAX + 2];
} _pcstate;

static int _printf_console_out(const char* str, size_t len, void* _state) {
    _pcstate* state = _state;
    char16_t* buf = state->buf;
    int i = state->off;
    int n = len;
    while (n > 0) {
        if (*str == '\n') {
            buf[i++] = '\r';
        }
        buf[i++] = *str++;
        if (i >= PCBUFMAX) {
            buf[i] = 0;
            gConOut->OutputString(gConOut, buf);
            i = 0;
        }
        n--;
    }
    state->off = i;
    return len;
}

int _printf(const char* fmt, ...) {
    va_list ap;
    _pcstate state;
    int r;
    state.off = 0;
    va_start(ap, fmt);
    r = _printf_engine(_printf_console_out, &state, fmt, ap);
    va_end(ap);
    if (state.off) {
        state.buf[state.off] = 0;
        gConOut->OutputString(gConOut, state.buf);
    }
    return r;
}

int puts16(char16_t* str) {
    return gConOut->OutputString(gConOut, str) == EFI_SUCCESS ? 0 : -1;
}
