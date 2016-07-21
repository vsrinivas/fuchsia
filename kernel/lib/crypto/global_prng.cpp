// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/global_prng.h>

#include <assert.h>
#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <lib/crypto/prng.h>

namespace crypto {

namespace GlobalPRNG {

PRNG* GetInstance() {
    static PRNG* global_prng = nullptr;
    static mutex_t lock = MUTEX_INITIAL_VALUE(lock);

    AutoLock guard(lock);
    if (unlikely(!global_prng)) {
        global_prng = new PRNG(nullptr, 0);
        ASSERT(global_prng);
    }
    return global_prng;
}

} //namespace GlobalPRNG

} // namespace crypto
