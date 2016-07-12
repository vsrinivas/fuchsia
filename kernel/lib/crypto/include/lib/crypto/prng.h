// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <lib/crypto/cryptolib.h>

namespace crypto {

// This exposes a cryptographically secure PRNG.
// This PRNG should be seeded with at least 256 bits of "real" entropy before
// being used for cryptographic applications.
class PRNG {
public:
    // The PRNG must be seeded. For cryptographic applications, 256 bits are
    // expected. |size| is in bytes.
    PRNG(const void* data, int size);
    ~PRNG();

    // Re-seed the PRNG by mixing-in new entropy. |size| is in bytes.
    void AddEntropy(const void* data, int size);

    // get pseudo-random output of |size| bytes.
    void Draw(void* out, int size);

private:
    PRNG(const PRNG&) = delete;
    PRNG& operator=(const PRNG&) = delete;

    clPRNG_CTX ctx_;
};

} // namespace crypto
