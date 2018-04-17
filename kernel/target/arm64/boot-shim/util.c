// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug.h"
#include "util.h"

typedef long word;

#define lsize sizeof(word)
#define lmask (lsize - 1)

// copied from kernel/lib/libc/string/memcpy.c
void *memcpy(void *dest, const void *src, size_t count) {
    char *d = (char *)dest;
    const char *s = (const char *)src;
    int len;

    if (count == 0 || dest == src)
        return dest;

    if (((long)d | (long)s) & lmask) {
        // src and/or dest do not align on word boundary
        if ((((long)d ^ (long)s) & lmask) || (count < lsize))
            len = count; // copy the rest of the buffer with the byte mover
        else
            len = lsize - ((long)d & lmask); // move the ptrs up to a word boundary

        count -= len;
        for (; len > 0; len--)
            *d++ = *s++;
    }
    for (len = count / lsize; len > 0; len--) {
        *(word *)d = *(word *)s;
        d += lsize;
        s += lsize;
    }
    for (len = count & lmask; len > 0; len--)
        *d++ = *s++;

    return dest;
}

// copied from kernel/lib/libc/string/strcmp.c
int strcmp(char const *cs, char const *ct) {
    signed char __res;

    while (1) {
        if ((__res = *cs - *ct++) != 0 || !*cs++)
            break;
    }

    return __res;
}

// copied from kernel/lib/libc/string/strncmp.c
int strncmp(char const *cs, char const *ct, size_t count) {
    signed char __res = 0;

    while (count > 0) {
        if ((__res = *cs - *ct++) != 0 || !*cs++)
            break;
        count--;
    }

    return __res;
}

void fail(const char* message) {
    uart_puts(message);
    while (1) {}
}
