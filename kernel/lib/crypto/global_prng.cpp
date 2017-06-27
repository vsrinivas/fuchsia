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
#include <mxcpp/new.h>
#include <mxtl/algorithm.h>
#include <lk/init.h>
#include <string.h>

#if ENABLE_ENTROPY_COLLECTOR_TEST
#include <kernel/cmdline.h>
#include <kernel/vm/vm_object_paged.h>
#endif

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
    const size_t hex_len = mxtl::min(strlen(entropy), kMaxEntropyArgumentLen);

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
    memset(const_cast<char*>(entropy), 'x', hex_len);
    if (hex_len >= sizeof(".redacted=") - 1) {
        memcpy(const_cast<char*>(entropy) - 1,
               ".redacted=", sizeof(".redacted=") - 1);
    }

    const size_t entropy_added = mxtl::max(hex_len / 2, sizeof(digest));
    return entropy_added;
}

#if ENABLE_ENTROPY_COLLECTOR_TEST

#ifndef ENTROPY_COLLECTOR_TEST_MAXLEN
// Default to 1 million bits (we're a kernel, not a hard disk!)
#define ENTROPY_COLLECTOR_TEST_MAXLEN (128u * 1024u)
#endif

namespace internal {

static uint8_t entropy_buf[ENTROPY_COLLECTOR_TEST_MAXLEN];
mxtl::RefPtr<VmObject> entropy_vmo;
bool entropy_was_lost = false;

static void SetupEntropyVmo(uint level) {
    if (VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, sizeof(entropy_buf),
                              &entropy_vmo) != MX_OK) {
        printf("entropy-record: Failed to create entropy_vmo (data lost)\n");
        entropy_was_lost = true;
        return;
    }
    size_t actual;
    if (entropy_vmo->Write(entropy_buf, 0, sizeof(entropy_buf), &actual)
            != MX_OK) {
        printf("entropy-record: Failed to write to entropy_vmo (data lost)\n");
        entropy_was_lost = true;
        return;
    }
    if (actual < sizeof(entropy_buf)) {
        printf("entropy-record: partial write to entropy_vmo (data lost)\n");
        entropy_was_lost = true;
        return;
    }
    constexpr const char *name = "debug/entropy.bin";
    if (entropy_vmo->set_name(name, strlen(name)) != MX_OK) {
        // The name is needed because devmgr uses it to add the VMO as a file in
        // the /boot filesystem.
        printf("entropy-record: could not name entropy_vmo (data lost)\n");
        entropy_was_lost = true;
        return;
    }
}

// Run the entropy collector test.
static void TestEntropyCollector() {
    constexpr size_t kMaxRead = 256;
    size_t read, total;
    ssize_t result;

    total = cmdline_get_uint64("kernel.entropy_test.len", sizeof(entropy_buf));
    if (total > sizeof(entropy_buf)) {
        total = sizeof(entropy_buf);
        printf("entropy-record: only recording %zu bytes (try defining "
               "ENTROPY_COLLECTOR_TEST_MAXLEN)\n", sizeof(entropy_buf));
    }

    read = 0;
    while (read < total) {
#if ARCH_X86_64
        result = hw_rng_get_entropy(
                entropy_buf + read,
                mxtl::min(kMaxRead, sizeof(entropy_buf) - read),
                true);
#else
        // Temporary workaround to suppress unused variable warning, only
        // because we don't yet have entropy on ARM.
        (void) kMaxRead;
        result = -1;
#endif

        if (result < 0) {
            // Failed to collect any entropy - report an error and give up.
            printf("entropy-record: source stopped returning entropy after "
                   "%zu bytes.\n", read);
            memset(entropy_buf + read, 0, sizeof(entropy_buf) - read);
            return;
        }
        read += result;
    }
}

} // namespace internal

#endif // ENABLE_ENTROPY_COLLECTOR_TEST

// Instantiates the global PRNG (in non-thread-safe mode) and seeds it.
static void EarlyBootSeed(uint level) {
    ASSERT(kGlobalPrng == nullptr);

#if ENABLE_ENTROPY_COLLECTOR_TEST
    // Before doing anything else, test our entropy collector. This is
    // explicitly called here rather than in another init hook to ensure
    // ordering (at level LK_INIT_LEVEL_TARGET_EARLY, but before the rest of
    // EarlyBootSeed).
    internal::TestEntropyCollector();
#endif

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

#if ENABLE_ENTROPY_COLLECTOR_TEST
LK_INIT_HOOK(setup_entropy_vmo, crypto::GlobalPRNG::internal::SetupEntropyVmo,
             LK_INIT_LEVEL_VM + 1);
#endif


LK_INIT_HOOK(global_prng_seed, crypto::GlobalPRNG::EarlyBootSeed,
             LK_INIT_LEVEL_TARGET_EARLY);

LK_INIT_HOOK(global_prng_thread_safe, crypto::GlobalPRNG::BecomeThreadSafe,
             LK_INIT_LEVEL_THREADING - 1)
