// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/encryption/primitives/rand.h"

#include <zircon/syscalls.h>

#include <atomic>

#include "lib/fxl/logging.h"
#include "lib/fxl/random/rand.h"

namespace encryption {

void RandBytes(void* buffer, size_t size) {
  FXL_CHECK(fxl::RandBytes(static_cast<uint8_t*>(buffer), size));
}

uint64_t RandUint64() {
  uint64_t result;
  RandBytes(&result, sizeof(result));
  return result;
}

}  // namespace encryption
