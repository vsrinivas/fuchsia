// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <lib/boot-options/types.h>
#include <lib/crypto/entropy/collector.h>
#include <lib/crypto/entropy/hw_rng_collector.h>
#include <lib/crypto/entropy/jitterentropy_collector.h>
#include <lib/crypto/entropy/quality_test.h>
#include <platform.h>
#include <string.h>
#include <zircon/types.h>

#include <dev/hw_rng.h>
#include <lk/init.h>
#include <vm/vm_object_paged.h>

namespace crypto {

namespace entropy {

#if ENABLE_ENTROPY_COLLECTOR_TEST

constexpr uint64_t GetMaxEntropyLength() {
#ifdef ENTROPY_COLLECTOR_TEST_MAXLEN
  return ENTROPY_COLLECTOR_TEST_MAXLEN;
#else
  return kMaxEntropyLength;
#endif
}

namespace {

uint8_t entropy_buf[GetMaxEntropyLength()];
size_t entropy_len;

}  // namespace

fbl::RefPtr<VmObjectPaged> entropy_vmo;
size_t entropy_vmo_content_size;
bool entropy_was_lost = false;

static void SetupEntropyVmo(uint level) {
  if (VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, entropy_len, &entropy_vmo) != ZX_OK) {
    printf("entropy-boot-test: Failed to create entropy_vmo (data lost)\n");
    entropy_was_lost = true;
    return;
  }
  if (entropy_vmo->Write(entropy_buf, 0, entropy_len) != ZX_OK) {
    printf("entropy-boot-test: Failed to write to entropy_vmo (data lost)\n");
    entropy_was_lost = true;
    return;
  }
  entropy_vmo_content_size = entropy_len;

  constexpr const char* name = "debug/entropy.bin";
  if (entropy_vmo->set_name(name, strlen(name)) != ZX_OK) {
    // The name is needed because devmgr uses it to add the VMO as a file in
    // the /boot filesystem.
    printf("entropy-boot-test: could not name entropy_vmo (data lost)\n");
    entropy_was_lost = true;
    return;
  }
}

// Run the entropy collector test.
void EarlyBootTest() {
  entropy::Collector* collector = nullptr;
  zx_status_t collector_result = ZX_OK;
  switch (gBootOptions->entropy_test_src) {
    case EntropyTestSource::kHwRng:
      collector_result = HwRngCollector::GetInstance(&collector);
      break;
    case EntropyTestSource::kJitterEntropy:
      collector_result = JitterentropyCollector::GetInstance(&collector);
      break;
  }

  // TODO(andrewkrieger): add other entropy collectors.

  if (collector_result != ZX_OK || collector == nullptr) {
    printf("entropy-boot-test: Failed to obtain entropy collector. Skipping test.\n");
    return;
  }

  entropy_len = gBootOptions->entropy_test_len;
  if (entropy_len > sizeof(entropy_buf)) {
    entropy_len = sizeof(entropy_buf);
    printf(
        "entropy-boot-test: only recording %zu bytes (try defining "
        "ENTROPY_COLLECTOR_TEST_MAXLEN)\n",
        sizeof(entropy_buf));
  }

  zx_time_t start = current_time();
  size_t result = collector->DrawEntropy(entropy_buf, entropy_len);
  zx_time_t end = current_time();

  if (result < entropy_len) {
    printf("entropy-boot-test: source only returned %zu bytes.\n", result);
    entropy_len = result;
  } else {
    printf("entropy-boot-test: successful draw in %" PRIu64 " nanoseconds.\n", end - start);
  }
}

#else  // ENABLE_ENTROPY_COLLECTOR_TEST

void EarlyBootTest() {}

#endif  // ENABLE_ENTROPY_COLLECTOR_TEST

}  // namespace entropy

}  // namespace crypto

#if ENABLE_ENTROPY_COLLECTOR_TEST
LK_INIT_HOOK(setup_entropy_vmo, crypto::entropy::SetupEntropyVmo, LK_INIT_LEVEL_VM + 1)
#endif
