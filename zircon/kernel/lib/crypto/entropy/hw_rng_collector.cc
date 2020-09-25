// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/crypto/entropy/hw_rng_collector.h>
#include <zircon/errors.h>

#include <dev/hw_rng.h>

namespace crypto {

namespace entropy {

static HwRngCollector instance;

zx_status_t HwRngCollector::GetInstance(Collector** ptr) {
  if (ptr == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (hw_rng_is_registered()) {
    *ptr = &instance;
    return ZX_OK;
  } else {
    *ptr = nullptr;
    return ZX_ERR_NOT_SUPPORTED;
  }
}

HwRngCollector::HwRngCollector() : Collector("hw_rng", /* entropy_per_1000_bytes */ 8000) {}

size_t HwRngCollector::DrawEntropy(uint8_t* buf, size_t len) {
  // Especially on systems that have RdRand but not RdSeed, avoid parallel
  // accesses. Per the Intel documentation, properly using RdRand to seed a
  // CPRNG requires careful access patterns, to avoid multiple RNG draws from
  // the same physical seed (see fxbug.dev/30929).
  Guard<Mutex> guard(&lock_);

  return hw_rng_get_entropy(buf, len);
}

}  // namespace entropy

}  // namespace crypto
