// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/prng.h>

#include <assert.h>
#include <compiler.h>
#include <err.h>
#include <lib/crypto/cryptolib.h>
#include <kernel/auto_lock.h>

namespace crypto {

PRNG::PRNG(const void* data, int size) {
    DEBUG_ASSERT(size >= 0);

    lock_ = MUTEX_INITIAL_VALUE(lock_);
    clPRNG_init(&ctx_, data, size);
    total_entropy_added_ = size;

    const bool enough_entropy = (total_entropy_added_ >= kMinEntropy);
    ready_ = EVENT_INITIAL_VALUE(ready_, enough_entropy, 0);
}

void PRNG::AddEntropy(const void* data, int size) {
    uint64_t total;
    {
        AutoLock guard(lock_);
        clPRNG_entropy(&ctx_, data, size);
        total_entropy_added_ += size;
        total = total_entropy_added_;
    }
    if (total >= kMinEntropy) {
        event_signal(&ready_, true);
    }
}

void PRNG::Draw(void* out, int size) {
    AutoLock guard(lock_);
    if (unlikely(total_entropy_added_ < kMinEntropy)) {
        mutex_release(&lock_);
        status_t status = event_wait(&ready_);
        ASSERT(status == NO_ERROR);
        mutex_acquire(&lock_);
    }
    clPRNG_draw(&ctx_, out, size);
}

PRNG::~PRNG() {}

} // namespace crypto
