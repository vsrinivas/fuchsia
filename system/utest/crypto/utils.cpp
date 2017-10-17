// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <crypto/bytes.h>
#include <zircon/types.h>

#include "utils.h"

namespace crypto {
namespace testing {

bool AllEqual(const void* buf, uint8_t val, zx_off_t off, size_t len) {
    size_t i;
    const uint8_t* u8 = static_cast<const uint8_t*>(buf);
    size_t end = off + len;
    ZX_ASSERT(end >= off); // overflow
    for (i = off; i < end && u8[i] == val; ++i) {
    }
    return i == end;
}

fbl::unique_ptr<uint8_t[]> MakeZeroPage() {
    fbl::unique_ptr<uint8_t[]> block;
    Bytes bytes;
    if (bytes.Init(PAGE_SIZE) == ZX_OK) {
        block = fbl::move(bytes.Release());
    }
    return block;
}

fbl::unique_ptr<uint8_t[]> MakeRandPage() {
    fbl::unique_ptr<uint8_t[]> block;
    Bytes bytes;
    if (bytes.Randomize(PAGE_SIZE) == ZX_OK) {
        block = fbl::move(bytes.Release());
    }
    return block;
}

} // namespace testing
} // namespace crypto
