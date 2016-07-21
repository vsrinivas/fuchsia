// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/event.h>
#include <kernel/mutex.h>
#include <lib/crypto/cryptolib.h>

namespace crypto {

// This exposes a thread-safe cryptographically secure PRNG.
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

    // Get pseudo-random output of |size| bytes.  Blocks until at least
    // kMinEntropy bytes of entropy have been added to this PRNG.
    void Draw(void* out, int size);

    // The minimum amount of entropy (in bytes) the generator requires before
    // Draw will return data.
    static constexpr uint64_t kMinEntropy = 32;
private:
    PRNG(const PRNG&) = delete;
    PRNG& operator=(const PRNG&) = delete;

    clPRNG_CTX ctx_;
    mutex_t lock_;
    uint64_t total_entropy_added_;
    event_t ready_;
};

} // namespace crypto
