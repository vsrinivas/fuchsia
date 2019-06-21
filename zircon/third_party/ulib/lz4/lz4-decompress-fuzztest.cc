// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <lz4/lz4.h>

static const size_t kMaxBufSize = 1024 * 1024 * 500; // 500 MiB

static char dstBuffer[kMaxBufSize] = {0};

// fuzz_target.cc
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static_assert(kMaxBufSize <= INT_MAX);
    if (size > INT_MAX) {
        return 0;
    }
    LZ4_decompress_safe(reinterpret_cast<const char*>(data), dstBuffer, static_cast<int>(size),
                        static_cast<int>(kMaxBufSize));
    return 0;
}
