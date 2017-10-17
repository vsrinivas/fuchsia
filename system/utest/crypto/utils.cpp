// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <string.h>

#include <crypto/bytes.h>
#include <crypto/digest.h>
#include <crypto/hkdf.h>
#include <crypto/hmac.h>
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

zx_status_t HexToBytes(const char* hex, Bytes* out) {
    zx_status_t rc;

    if (!hex || !out) {
        return ZX_ERR_INVALID_ARGS;
    }
    size_t len = strlen(hex);
    if (len % 2 != 0) {
        return ZX_ERR_INVALID_ARGS;
    }
    out->Reset();
    if ((rc = out->Resize(len / 2)) != ZX_OK) {
        return rc;
    }
    size_t i = 0;
    size_t j = 0;
    uint8_t n;
    while (i < len) {
        char c = hex[i];
        if ('0' <= c && c <= '9') {
            n = static_cast<uint8_t>(c - '0');
        } else if ('a' <= c && c <= 'f') {
            n = static_cast<uint8_t>(c - 'a' + 10);
        } else if ('A' <= c && c <= 'F') {
            n = static_cast<uint8_t>(c - 'A' + 10) ;
        } else {
            return ZX_ERR_INVALID_ARGS;
        }
        if (i % 2 == 0) {
            (*out)[j] = static_cast<uint8_t>(n << 4);
        } else {
            (*out)[j] |= n & 0xF;
            ++j;
        }
        ++i;
    }
    return ZX_OK;
}

} // namespace testing
} // namespace crypto
