// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/event.h>
#include <lib/crypto/cryptolib.h>
#include <fbl/mutex.h>

namespace crypto {

// This exposes a (optionally-)thread-safe cryptographically secure PRNG.
// This PRNG must be seeded with at least 256 bits of "real" entropy before
// being used for cryptographic applications.
class PRNG {
public:
    // Tag object for constructing a non-thread-safe version.
    struct NonThreadSafeTag {};

    // Construct a thread-safe instance of the PRNG with the byte array at
    // |data| as the initial seed.  |size| is the length of |data| in bytes.
    PRNG(const void* data, size_t size);

    // Construct a non-thread-safe instance of the PRNG with the byte array at
    // |data| as the initial seed.  |size| is the length of |data| in bytes.
    PRNG(const void* data, size_t size, NonThreadSafeTag);

    ~PRNG();

    // Transitions the PRNG to thread-safe mode.  This asserts that the
    // instance is not yet thread-safe.
    void BecomeThreadSafe();

    // Re-seed the PRNG by mixing-in new entropy. |size| is in bytes.  |data|
    // should be high-quality entropy, i.e. each bit should have equal
    // probability of being 0 or 1. |size| MUST NOT be greater than kMaxEntropy.
    void AddEntropy(const void* data, size_t size);

    // Get pseudo-random output of |size| bytes.  Blocks until at least
    // kMinEntropy bytes of entropy have been added to this PRNG.  |size| MUST
    // NOT be greater than kMaxDrawLen.
    void Draw(void* out, size_t size);

    // Return an integer in the range [0, exclusive_upper_bound) chosen
    // uniformly at random.  This is a wrapper for Draw(), and so has the same
    // caveats.
    uint64_t RandInt(uint64_t exclusive_upper_bound);

    // Inspect if this PRNG is threadsafe.  Only really useful for test code.
    bool is_thread_safe() const { return is_thread_safe_; }

    // The minimum amount of entropy (in bytes) the generator requires before
    // Draw will return data.
    static constexpr uint64_t kMinEntropy = 32;

    // The maximum amount of entropy (in bytes) that can be submitted to
    // AddEntropy.  Anything above this will panic.
    static constexpr uint64_t kMaxEntropy = 1ULL << 30;

    // The maximum amount of pseudorandom data (in bytes) that can be drawn in
    // one call to Draw.  This the limit imposed by the maximum number of bytes
    // that can be generated with a single key/nonce pair. Each request to Draw
    // uses a different key/nonce pair.  Anything above this will panic.
    static constexpr uint64_t kMaxDrawLen = 1ULL << 38;

private:
    PRNG(const PRNG&) = delete;
    PRNG& operator=(const PRNG&) = delete;

    // Reseeds the number generator with the given seed.  This method is not
    // thread safe.  Returns true if blocked Draw calls can resume, else false.
    void AddEntropyInternal(const void* data, size_t size);

    // Generates pseudorandom bytes and writes them to |out|.  This method is
    // not thread-safe.
    void DrawInternal(void* out, size_t size);

    // ChaCha20 key.
    uint8_t key_[clSHA256_DIGEST_SIZE];

    // 96-bit ChaCha20 nonce as described in RFC 7539, represented as unsigned
    // integers of different sizes for implementation convenience. Section 9.5/1
    // of the c++11 spec guarantees each member below starts at the same
    // location, meaning u64 is the same as u32[0:1] and u8[0:8].  The nonce is
    // updated with every call to Draw.
    union {
        uint64_t u64;
        uint32_t u32[3];
        uint8_t u8[12];
    } nonce_;

    bool is_thread_safe_;
    fbl::Mutex lock_;
    uint64_t total_entropy_added_;
    event_t ready_;
};

} // namespace crypto
