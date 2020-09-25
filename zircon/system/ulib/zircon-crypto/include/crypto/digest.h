// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTO_DIGEST_H_
#define CRYPTO_DIGEST_H_

#include <stddef.h>
#include <zircon/types.h>

// |crypto::digest| is a cryptographic message digest algorithm.
// TODO(aarongreen): fxbug.dev/31188: Merge ulib/digest with ulib/crypto
namespace crypto {
namespace digest {

// Algorithm enumerates the supported message digests
enum Algorithm {
  kUninitialized = 0,
  kSHA256,
};

// Gets a pointer to the opaque crypto implementation of the digest algorithm.
zx_status_t GetDigest(Algorithm digest, uintptr_t* out);

// Gets the number of bytes needed for the digest produced by the given |version|.
zx_status_t GetDigestLen(Algorithm digest, size_t* out);

}  // namespace digest
}  // namespace crypto

#endif  // CRYPTO_DIGEST_H_
