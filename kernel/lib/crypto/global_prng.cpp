// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/global_prng.h>

#include <assert.h>
#include <dev/hw_rng.h>
#include <err.h>
#include <kernel/auto_lock.h>
#include <kernel/mutex.h>
#include <lib/crypto/prng.h>
#include <lk/init.h>

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

static void EarlyBootSeed(uint level) {
    PRNG* prng = GetInstance();

    uint8_t buf[32] = {0};
    // TODO(security): Have the PRNG reseed based on usage
    size_t fetched = hw_rng_get_entropy(buf, sizeof(buf), true);
    if (fetched == 0) {
        // We made a blocking request, but it returned 0, so there is no PRNG
        printf("WARNING: System has no entropy source.  It is completely "
               "unsafe to use this system for any cryptographic applications."
               "\n");
        // TODO(security): *CRITICAL* This is a fallback for systems without RNG
        // hardware that we should remove and attempt to do better.  If this
        // fallback is used, it breaks all cryptography used on the system.
        // *CRITICAL*
        prng->AddEntropy(buf, sizeof(buf));
        return;
    }
    DEBUG_ASSERT(fetched == sizeof(buf));
    prng->AddEntropy(buf, static_cast<int>(fetched));
}

} //namespace GlobalPRNG

} // namespace crypto

LK_INIT_HOOK(global_prng, crypto::GlobalPRNG::EarlyBootSeed,
             LK_INIT_LEVEL_THREADING);
