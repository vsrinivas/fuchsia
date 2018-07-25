// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include <string.h>

#include <zircon/assert.h>

namespace optee {

UuidView::UuidView(const uint8_t* data, size_t size)
    : ptr_{data} {
    ZX_DEBUG_ASSERT(data);
    ZX_DEBUG_ASSERT(size == kUuidSize);
}

void UuidView::ToUint64Pair(uint64_t* out_hi, uint64_t* out_low) const {
    ZX_DEBUG_ASSERT(out_hi);
    ZX_DEBUG_ASSERT(out_low);

    // REE and TEE always share the same endianness so the treatment of UUID bytes is the same on
    // both sides.
    ::memcpy(out_hi, ptr_, sizeof(*out_hi));
    ::memcpy(out_low, ptr_ + sizeof(*out_hi), sizeof(*out_low));
}

} // namespace optee
