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

// This exposes a (optionally-)thread-safe cryptographically secure PRNG.
// This PRNG must be seeded with at least 256 bits of "real" entropy before
// being used for cryptographic applications.
class PRNG {
public:
    // Tag object for constructing a non-thread-safe version.
    struct NonThreadSafeTag { };

    // Construct a thread-safe instance of the PRNG with the byte array at
    // |data| as the initial seed.  |size| is the length of |data| in bytes.
    PRNG(const void* data, int size);

    // Construct a non-thread-safe instance of the PRNG with the byte array at
    // |data| as the initial seed.  |size| is the length of |data| in bytes.
    PRNG(const void* data, int size, NonThreadSafeTag);

    ~PRNG();

    // Transitions the PRNG to thread-safe mode.  This asserts that the
    // instance is not yet thread-safe.
    void BecomeThreadSafe();

    // Re-seed the PRNG by mixing-in new entropy. |size| is in bytes.
    void AddEntropy(const void* data, int size);

    // Get pseudo-random output of |size| bytes.  Blocks until at least
    // kMinEntropy bytes of entropy have been added to this PRNG.
    void Draw(void* out, int size);

    // Return an integer in the range [0, exclusive_upper_bound) chosen
    // uniformly at random.  This is a wrapper for Draw(), and so has the same
    // caveats.
    uint64_t RandInt(uint64_t exclusive_upper_bound);

    // Inspect if this PRNG is threadsafe.  Only really useful for test code.
    bool is_thread_safe() const { return is_thread_safe_; }

    // The minimum amount of entropy (in bytes) the generator requires before
    // Draw will return data.
    static constexpr uint64_t kMinEntropy = 32;
private:
    PRNG(const PRNG&) = delete;
    PRNG& operator=(const PRNG&) = delete;

    bool is_thread_safe_;
    clPRNG_CTX ctx_;
    Mutex lock_;
    uint64_t total_entropy_added_;
    event_t ready_;
};

} // namespace crypto
