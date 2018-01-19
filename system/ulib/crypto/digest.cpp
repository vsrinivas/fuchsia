// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <crypto/digest.h>
#include <fdio/debug.h>
#include <openssl/digest.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#define ZXDEBUG 0

namespace crypto {
namespace digest {

// Gets a pointer to the opaque crypto implementation of the digest algorithm.
zx_status_t GetDigest(Algorithm digest, uintptr_t* out) {
    ZX_DEBUG_ASSERT(out);
    const EVP_MD* md;
    switch (digest) {
    case digest::kUninitialized:
        xprintf("not initialized\n");
        return ZX_ERR_INVALID_ARGS;

    case digest::kSHA256:
        md = EVP_sha256();
        break;

    default:
        xprintf("invalid digest = %u\n", digest);
        return ZX_ERR_NOT_SUPPORTED;
    }
    *out = reinterpret_cast<uintptr_t>(md);

    return ZX_OK;
}

// Gets the minimum number of bytes needed for the digest produced by the given |version|.
zx_status_t GetDigestLen(Algorithm digest, size_t* out) {
    zx_status_t rc;

    ZX_DEBUG_ASSERT(out);
    uintptr_t ptr;
    if ((rc = GetDigest(digest, &ptr)) != ZX_OK) {
        return rc;
    }
    *out = EVP_MD_size(reinterpret_cast<const EVP_MD*>(ptr));

    return ZX_OK;
}

} // namespace digest
} // namespace crypto
