// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/glue/crypto/rand.h"

#include <openssl/rand.h>

#include "lib/ftl/logging.h"

namespace glue {

void RandBytes(void* buffer, size_t size) {
  FTL_CHECK(RAND_bytes(static_cast<uint8_t*>(buffer), size) == 1);
}

uint64_t RandUint64() {
  uint64_t result;
  RandBytes(&result, sizeof(result));
  return result;
}

}  // namespace glue
