// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/glue/crypto/rand.h"

#include <zircon/syscalls.h>

#include <atomic>

#include "lib/fxl/logging.h"
#include "lib/fxl/random/rand.h"

namespace glue {

namespace {

void InitEntropy() {
  auto current_time = zx_time_get(ZX_CLOCK_UTC);
  if (zx_cprng_add_entropy(&current_time, sizeof(current_time)) != ZX_OK) {
    FXL_LOG(WARNING)
        << "Unable to add entropy to the kernel. No additional entropy added.";
    return;
  }
}

void EnsureInitEntropy() {
  static std::atomic<bool> initialized(false);
  if (initialized)
    return;
  InitEntropy();
  initialized = true;
}
}  // namespace

void RandBytes(void* buffer, size_t size) {
  EnsureInitEntropy();
  FXL_CHECK(fxl::RandBytes(static_cast<uint8_t*>(buffer), size));
}

uint64_t RandUint64() {
  uint64_t result;
  RandBytes(&result, sizeof(result));
  return result;
}

}  // namespace glue
