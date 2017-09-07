// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy/quality_test.h>

#include <dev/hw_rng.h>
#include <kernel/cmdline.h>
#include <vm/vm_object_paged.h>
#include <lib/crypto/entropy/collector.h>
#include <lib/crypto/entropy/hw_rng_collector.h>
#include <lib/crypto/entropy/jitterentropy_collector.h>
#include <lk/init.h>
#include <magenta/types.h>
#include <string.h>

namespace crypto {

namespace entropy {

#if ENABLE_ENTROPY_COLLECTOR_TEST

#ifndef ENTROPY_COLLECTOR_TEST_MAXLEN
// Default to 1 million bits (we're a kernel, not a hard disk!)
#define ENTROPY_COLLECTOR_TEST_MAXLEN (128u * 1024u)
#endif

static uint8_t entropy_buf[ENTROPY_COLLECTOR_TEST_MAXLEN];
fbl::RefPtr<VmObject> entropy_vmo;
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
void EarlyBootTest() {
    const char* src_name = cmdline_get("kernel.entropy-test.src");
    if (!src_name) {
        src_name = "";
    }

    entropy::Collector* collector = nullptr;
    entropy::Collector* candidate;
    char candidate_name[MX_MAX_NAME_LEN];

    // TODO(andrewkrieger): find a nicer way to enumerate all entropy collectors
    if (HwRngCollector::GetInstance(&candidate) == MX_OK) {
        candidate->get_name(candidate_name, sizeof(candidate_name));
        if (strncmp(candidate_name, src_name, MX_MAX_NAME_LEN) == 0) {
            collector = candidate;
        }
    }
    if (!collector &&
        JitterentropyCollector::GetInstance(&candidate) == MX_OK) {
        candidate->get_name(candidate_name, sizeof(candidate_name));
        if (strncmp(candidate_name, src_name, MX_MAX_NAME_LEN) == 0) {
            collector = candidate;
        }
    }

    // TODO(andrewkrieger): add other entropy collectors.

    if (!collector) {
        printf("entropy-test: unrecognized source \"%s\"\n", src_name);
        printf("entropy-test: skipping test.\n");
        return;
    }

    size_t len = cmdline_get_uint64("kernel.entropy-test.len",
                                    sizeof(entropy_buf));
    if (len > sizeof(entropy_buf)) {
        len = sizeof(entropy_buf);
        printf("entropy-test: only recording %zu bytes (try defining "
               "ENTROPY_COLLECTOR_TEST_MAXLEN)\n", sizeof(entropy_buf));
    }

    size_t result = collector->DrawEntropy(entropy_buf, len);
    if (result < len) {
        printf("entropy-test: source only returned %zu bytes.\n", result);
        memset(entropy_buf + result, 0, sizeof(entropy_buf) - result);
    }
}


#else // ENABLE_ENTROPY_COLLECTOR_TEST

void EarlyBootTest() {
}

#endif // ENABLE_ENTROPY_COLLECTOR_TEST

} // namespace entropy

} // namespace crypto

#if ENABLE_ENTROPY_COLLECTOR_TEST
LK_INIT_HOOK(setup_entropy_vmo, crypto::entropy::SetupEntropyVmo,
             LK_INIT_LEVEL_VM + 1);
#endif
