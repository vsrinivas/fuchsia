// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/global_prng.h>

#include <assert.h>
#include <ctype.h>
#include <dev/hw_rng.h>
#include <err.h>
#include <kernel/auto_lock.h>
#include <kernel/cmdline.h>
#include <kernel/mutex.h>
#include <lib/crypto/cryptolib.h>
#include <lib/crypto/prng.h>
#include <new.h>
#include <mxtl/algorithm.h>
#include <lk/init.h>
#include <string.h>

namespace crypto {

namespace GlobalPRNG {

static PRNG* kGlobalPrng = nullptr;

PRNG* GetInstance() {
    ASSERT(kGlobalPrng);
    return kGlobalPrng;
}

// TODO(security): Remove this in favor of virtio-rng once it is available and
// we decide we don't need it for getting entropy from elsewhere.
static size_t IntegrateCmdlineEntropy() {
    const char* entropy = cmdline_get("kernel.entropy");
    if (!entropy) {
        return 0;
    }

    const size_t kMaxEntropyArgumentLen = 128;
    const int hex_len = static_cast<int>(mxtl::min(strlen(entropy), kMaxEntropyArgumentLen));

    for (int i = 0; i < hex_len; ++i) {
        if (!isxdigit(entropy[i])) {
            panic("Invalid entropy string: idx %d is not an ASCII hex digit\n", i);
        }
    }

    uint8_t digest[clSHA256_DIGEST_SIZE];
    clSHA256(entropy, hex_len, digest);
    kGlobalPrng->AddEntropy(digest, sizeof(digest));

    // TODO(security): Use HideFromCompiler() once we implement it.
    // Make a best effort to clear this out so it isn't sent to usermode
    memset(const_cast<char*>(entropy), '0', hex_len);

    const size_t entropy_added = mxtl::max(static_cast<size_t>(hex_len / 2), sizeof(digest));
    return entropy_added;
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
    kGlobalPrng = new (&prng_space) PRNG(nullptr, 0, PRNG::NonThreadSafeTag());

    uint8_t buf[PRNG::kMinEntropy] = {0};
    // TODO(security): Have the PRNG reseed based on usage
    size_t fetched = 0;
#if ARCH_X86_64
    // We currently only have a hardware RNG implemented for x86-64.  If
    // we're on ARM, we will probably go through the fallback (see the
    // security warning below).
    fetched = hw_rng_get_entropy(buf, sizeof(buf), true);
#endif

    if (fetched != 0) {
        DEBUG_ASSERT(fetched == sizeof(buf));
        kGlobalPrng->AddEntropy(buf, static_cast<int>(fetched));
    }

    fetched += IntegrateCmdlineEntropy();
    if (fetched < PRNG::kMinEntropy) {
        printf("WARNING: System has insufficient randomness.  It is completely "
               "unsafe to use this system for any cryptographic applications."
               "\n");
        // TODO(security): *CRITICAL* This is a fallback for systems without RNG
        // hardware that we should remove and attempt to do better.  If this
        // fallback is used, it breaks all cryptography used on the system.
        // *CRITICAL*
        kGlobalPrng->AddEntropy(buf, sizeof(buf));
        return;
    }
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
