// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

/* do a hex dump against stdout 32bits and 8bits at a time */
void _hexdump_ex(const void* ptr, size_t len, uint64_t disp_addr);
void _hexdump8_ex(const void* ptr, size_t len, uint64_t disp_addr);

static inline void hexdump(const void* ptr, size_t len) {
    _hexdump_ex(ptr, len, (uint64_t)((uintptr_t)ptr));
}

static inline void hexdump8(const void* ptr, size_t len) {
    _hexdump8_ex(ptr, len, (uint64_t)((uintptr_t)ptr));
}
