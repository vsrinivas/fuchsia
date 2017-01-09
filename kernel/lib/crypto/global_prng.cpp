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
#include <new.h>
#include <lk/init.h>

namespace crypto {

namespace GlobalPRNG {

static PRNG* kGlobalPrng = nullptr;

PRNG* GetInstance() {
    ASSERT(kGlobalPrng);
    return kGlobalPrng;
}

// Instantiates the global PRNG (in non-thread-safe mode) and seeds it.
static void EarlyBootSeed(uint level) {
    ASSERT(kGlobalPrng == nullptr);

    // Statically allocate an array of bytes to put the PRNG into.  We do this
    // to control when the PRNG constructor is called.
    // TODO(security): This causes the PRNG state to be in a fairly predictable
    // place.  Some aspects of KASLR will help with this, but we may
    // additionally want to remap where this is later.
    alignas(alignof(PRNG))static uint8_t prng_space[sizeof(PRNG)];
    kGlobalPrng = new (&prng_space) PRNG(NULL, 0, PRNG::NonThreadSafeTag());

    uint8_t buf[32] = {0};
    // TODO(security): Have the PRNG reseed based on usage
    size_t fetched = 0;
#if ARCH_X86_64
    // We currently only have a hardware RNG implemented for x86-64.  If
    // we're on ARM, go through the fallback (see the security warning below).
    fetched = hw_rng_get_entropy(buf, sizeof(buf), true);
#endif
    if (fetched == 0) {
        // We made a blocking request, but it returned 0, so there is no PRNG
        printf("WARNING: System has no entropy source.  It is completely "
               "unsafe to use this system for any cryptographic applications."
               "\n");
        // TODO(security): *CRITICAL* This is a fallback for systems without RNG
        // hardware that we should remove and attempt to do better.  If this
        // fallback is used, it breaks all cryptography used on the system.
        // *CRITICAL*
        kGlobalPrng->AddEntropy(buf, sizeof(buf));
        return;
    }
    DEBUG_ASSERT(fetched == sizeof(buf));
    kGlobalPrng->AddEntropy(buf, static_cast<int>(fetched));
}

// Migrate the global PRNG to enter thread-safe mode.
static void BecomeThreadSafe(uint level) {
    GetInstance()->BecomeThreadSafe();
}

} //namespace GlobalPRNG

} // namespace crypto

LK_INIT_HOOK(global_prng_seed, crypto::GlobalPRNG::EarlyBootSeed,
             LK_INIT_LEVEL_TARGET_EARLY);

LK_INIT_HOOK(global_prng_thread_safe, crypto::GlobalPRNG::BecomeThreadSafe,
             LK_INIT_LEVEL_THREADING - 1)
