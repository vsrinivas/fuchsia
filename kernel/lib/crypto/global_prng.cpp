// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/global_prng.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <explicit-memory/bytes.h>
#include <fbl/algorithm.h>
#include <kernel/auto_lock.h>
#include <kernel/cmdline.h>
#include <kernel/mutex.h>
#include <lib/crypto/cryptolib.h>
#include <lib/crypto/entropy/collector.h>
#include <lib/crypto/entropy/jitterentropy_collector.h>
#include <lib/crypto/entropy/hw_rng_collector.h>
#include <lib/crypto/entropy/quality_test.h>
#include <lib/crypto/prng.h>
#include <mxcpp/new.h>
#include <lk/init.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

namespace crypto {

namespace GlobalPRNG {

static PRNG* kGlobalPrng = nullptr;

PRNG* GetInstance() {
    ASSERT(kGlobalPrng);
    return kGlobalPrng;
}

// Returns true if the kernel cmdline provided at least PRNG::kMinEntropy bytes
// of entropy, and false otherwise.
//
// TODO(security): Remove this in favor of virtio-rng once it is available and
// we decide we don't need it for getting entropy from elsewhere.
static bool IntegrateCmdlineEntropy() {
    const char* entropy = cmdline_get("kernel.entropy-mixin");
    if (!entropy) {
        return false;
    }

    const size_t kMaxEntropyArgumentLen = 128;
    const size_t hex_len = fbl::min(strlen(entropy), kMaxEntropyArgumentLen);

    for (size_t i = 0; i < hex_len; ++i) {
        if (!isxdigit(entropy[i])) {
            panic("Invalid entropy string: idx %zu is not an ASCII hex digit\n", i);
        }
    }

    uint8_t digest[clSHA256_DIGEST_SIZE];
    clSHA256(entropy, static_cast<int>(hex_len), digest);
    kGlobalPrng->AddEntropy(digest, sizeof(digest));

    // We have a pointer to const, but it's actually a pointer to the
    // mutable global state in __kernel_cmdline that is still live (it
    // will be copied into the userboot bootstrap message later).  So
    // it's fully well-defined to cast away the const and mutate this
    // here so the bits can't leak to userboot.  While we're at it,
    // prettify the result a bit so it's obvious what one is looking at.
    mandatory_memset(const_cast<char*>(entropy), 'x', hex_len);
    if (hex_len >= sizeof(".redacted=") - 1) {
        memcpy(const_cast<char*>(entropy) - 1,
               ".redacted=", sizeof(".redacted=") - 1);
    }

    const size_t entropy_added = fbl::max(hex_len / 2, sizeof(digest));
    LTRACEF("Collected %zu bytes of entropy from the kernel cmdline.\n",
            entropy_added);
    return (entropy_added >= PRNG::kMinEntropy);
}

// Returns true on success, false on failure.
static bool SeedFrom(entropy::Collector* collector) {
    uint8_t buf[PRNG::kMinEntropy] = {0};
    size_t remaining = collector->BytesNeeded(8 * PRNG::kMinEntropy);
#if LOCAL_TRACE
    {
        char name[MX_MAX_NAME_LEN];
        collector->get_name(name, sizeof(name));
        LTRACEF("About to collect %zu bytes of entropy from '%s'.\n",
                remaining, name);
    }
#endif
    while (remaining > 0) {
        size_t result = collector->DrawEntropy(
                buf, fbl::min(sizeof(buf), remaining));
        if (result == 0) {
            LTRACEF("Collected 0 bytes; aborting. "
                    "There were %zu bytes remaining to collect.\n",
                    remaining);
            return false;
        }
        // TODO(MG-1007): don't assume that every byte of entropy that's added
        // has a full 8 bits worth of entropy
        kGlobalPrng->AddEntropy(buf, result);
        mandatory_memset(buf, 0, sizeof(buf));
        remaining -= result;
    }
    LTRACEF("Successfully collected entropy.\n");
    return true;
}

// Instantiates the global PRNG (in non-thread-safe mode) and seeds it.
static void EarlyBootSeed(uint level) {
    ASSERT(kGlobalPrng == nullptr);

    // Before doing anything else, test our entropy collector. This is
    // explicitly called here rather than in another init hook to ensure
    // ordering (at level LK_INIT_LEVEL_TARGET_EARLY, but before the rest of
    // EarlyBootSeed).
    entropy::EarlyBootTest();

    // Statically allocate an array of bytes to put the PRNG into.  We do this
    // to control when the PRNG constructor is called.
    // TODO(security): This causes the PRNG state to be in a fairly predictable
    // place.  Some aspects of KASLR will help with this, but we may
    // additionally want to remap where this is later.
    alignas(alignof(PRNG))static uint8_t prng_space[sizeof(PRNG)];
    kGlobalPrng = new (&prng_space) PRNG(nullptr, 0, PRNG::NonThreadSafeTag());

    // TODO(security): Have the PRNG reseed based on usage

    unsigned int successful = 0; // number of successful entropy sources
    entropy::Collector* collector;
    if (entropy::HwRngCollector::GetInstance(&collector) == MX_OK &&
        SeedFrom(collector)) {
        successful++;
    }
    if (entropy::JitterentropyCollector::GetInstance(&collector) == MX_OK &&
        SeedFrom(collector)) {
        successful++;
    }

    if (IntegrateCmdlineEntropy()) {
        successful++;
    }
    if (successful == 0) {
        printf("WARNING: System has insufficient randomness.  It is completely "
               "unsafe to use this system for any cryptographic applications."
               "\n");
        // TODO(security): *CRITICAL* This is a fallback for systems without RNG
        // hardware that we should remove and attempt to do better.  If this
        // fallback is used, it breaks all cryptography used on the system.
        // *CRITICAL*
        uint8_t buf[PRNG::kMinEntropy] = {0};
        kGlobalPrng->AddEntropy(buf, sizeof(buf));
        return;
    } else {
        LTRACEF("Successfully collected entropy from %u sources.\n",
                successful);
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
