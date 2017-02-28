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
