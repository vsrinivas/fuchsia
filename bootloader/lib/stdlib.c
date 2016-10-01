// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <ctype.h>
#include <stdint.h>

#define ATOx(T, fn)             \
T fn(const char* nptr) {            \
    while (nptr && isspace(*nptr)) {  \
        nptr++;                       \
    }                                 \
                                      \
    bool neg = false;                 \
    if (*nptr == '-') {               \
        neg = true;                   \
        nptr++;                       \
    }                                 \
                                      \
    T ret = 0;                        \
    for (; nptr; nptr++) {            \
        if (!isdigit(*nptr)) break;   \
        ret = 10*ret + (*nptr - '0'); \
    }                                 \
                                      \
    if (neg) ret = -ret;              \
    return ret;                       \
}


ATOx(int, atoi) 
ATOx(long, atol)
ATOx(long long, atoll)
