// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/prng.h>

#include <assert.h>
#include <magenta/compiler.h>
#include <err.h>
#include <lib/crypto/cryptolib.h>
#include <kernel/auto_lock.h>
#include <pow2.h>

namespace crypto {

PRNG::PRNG(const void* data, int size)
    : PRNG(data, size, NonThreadSafeTag()) {

    BecomeThreadSafe();
}

PRNG::PRNG(const void* data, int size, NonThreadSafeTag tag)
    : is_thread_safe_(false), lock_(), total_entropy_added_(size) {

    DEBUG_ASSERT(size >= 0);
    clPRNG_init(&ctx_, data, size);
}

void PRNG::AddEntropy(const void* data, int size) {
    if (likely(is_thread_safe_)) {
        uint64_t total;
        {
            AutoLock guard(&lock_);
            clPRNG_entropy(&ctx_, data, size);
            total_entropy_added_ += size;
            total = total_entropy_added_;
        }
        if (total >= kMinEntropy) {
            event_signal(&ready_, true);
        }
    } else {
        clPRNG_entropy(&ctx_, data, size);
        total_entropy_added_ += size;
    }
}

void PRNG::Draw(void* out, int size) {
    if (likely(is_thread_safe_)) {
        AutoLock guard(&lock_);
        if (unlikely(total_entropy_added_ < kMinEntropy)) {
            lock_.Release();
            status_t status = event_wait(&ready_);
            ASSERT(status == NO_ERROR);
            lock_.Acquire();
        }
        clPRNG_draw(&ctx_, out, size);
    } else {
        clPRNG_draw(&ctx_, out, size);
    }
}

uint64_t PRNG::RandInt(uint64_t exclusive_upper_bound) {
    ASSERT(exclusive_upper_bound != 0);

    const uint log2 = log2_ulong_ceil(exclusive_upper_bound);
    const size_t mask = (log2 != sizeof(uint64_t) * CHAR_BIT) ?
            (uint64_t(1) << log2) - 1 :
            UINT64_MAX;
    DEBUG_ASSERT(exclusive_upper_bound - 1 <= mask);

    // This loop should terminate very fast, since the probability that the
    // drawn value is >= exclusive_upper_bound is less than 0.5.  This is the
    // classic discard out-of-range values approach.
    while (true) {
        uint64_t v;
        Draw(reinterpret_cast<uint8_t*>(&v), sizeof(uint64_t)/sizeof(uint8_t));
        v &= mask;
        if (v < exclusive_upper_bound) {
            return v;
        }
    }
}

// It is safe to call this function from PRNG's constructor provided
// |is_thread_safe_| and |total_entropy_added_| are initialized.
void PRNG::BecomeThreadSafe() {
    ASSERT(!is_thread_safe_);

    const bool enough_entropy = (total_entropy_added_ >= kMinEntropy);
    ready_ = EVENT_INITIAL_VALUE(ready_, enough_entropy, 0);

    is_thread_safe_ = true;
}

PRNG::~PRNG() {}

} // namespace crypto
