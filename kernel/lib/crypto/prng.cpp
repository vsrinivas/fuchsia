// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/prng.h>

#include <lib/crypto/cryptolib.h>
#include <kernel/auto_lock.h>

namespace crypto {

PRNG::PRNG(const void* data, int size) {
    lock_ = MUTEX_INITIAL_VALUE(lock_);
    clPRNG_init(&ctx_, data, size);
}

void PRNG::AddEntropy(const void* data, int size) {
    AutoLock guard(lock_);
    clPRNG_entropy(&ctx_, data, size);
}

void PRNG::Draw(void* out, int size) {
    AutoLock guard(lock_);
    clPRNG_draw(&ctx_, out, size);
}

PRNG::~PRNG() {}

} // namespace crypto
