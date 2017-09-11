// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/crypto/rand.h"

#include <magenta/syscalls.h>

#include <atomic>

#include "lib/fxl/logging.h"
#include "lib/fxl/random/rand.h"

namespace glue {

namespace {

void InitEntropy() {
  auto current_time = mx_time_get(MX_CLOCK_UTC);
  if (mx_cprng_add_entropy(&current_time, sizeof(current_time)) != MX_OK) {
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
