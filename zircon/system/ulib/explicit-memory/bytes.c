// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <explicit-memory/bytes.h>

void* mandatory_memcpy(void* dst, const void* src, size_t n) {
    volatile unsigned char* out = dst;
    volatile const unsigned char* in = src;
    for (size_t i = 0; i < n; ++i) {
        out[i] = in[i];
    }
    return dst;
}

void* mandatory_memset(void* dst, int c, size_t n) {
    volatile unsigned char* out = dst;
    for (size_t i = 0; i < n; ++i) {
        out[i] = (unsigned char)c;
    }
    return dst;
}
