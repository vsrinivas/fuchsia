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

static const size_t kMaxBufSize = 1024 * 1024 * 100; // 100 MiB

static char compressedData[kMaxBufSize] = {0};
static char decompressedData[kMaxBufSize] = {0};

// fuzz_target.cc
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static_assert(kMaxBufSize <= INT_MAX);
    if (size > kMaxBufSize) {
        return 0;
    }

    // src will be data to compress. No need to be NULL-terminated.
    const char* src = reinterpret_cast<const char*>(data);
    int srcSize = static_cast<int>(size);

    int dstSize = LZ4_compressBound(srcSize);
    assert(dstSize > 0);
    assert(dstSize <= static_cast<int>(kMaxBufSize));

    int compressedSize = LZ4_compress_default(src, compressedData, srcSize, dstSize);
    // compression is guaranteed to succeed if dstSize <= LZ4_compressBound(srcSize).
    assert(compressedSize > 0);

    int decompressedSize =
        LZ4_decompress_safe(compressedData, decompressedData, compressedSize, kMaxBufSize);

    assert(decompressedSize == srcSize);
    assert(memcmp(data, decompressedData, size) == 0);

    return 0;
}
