// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/glue/crypto/rand.h"

#include <magenta/syscalls.h>
#include <openssl/rand.h>
#include <sys/time.h>

#include <atomic>

#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace glue {

namespace {

void InitEntropy() {
  struct timeval tv;

  if (gettimeofday(&tv, nullptr) != 0) {
    FTL_LOG(WARNING)
        << "Unable to call gettimeofday. No additional entropy added.";
  }
  if (mx_cprng_add_entropy(&tv, sizeof(tv)) != NO_ERROR) {
    FTL_LOG(WARNING)
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

}

void RandBytes(void* buffer, size_t size) {
  EnsureInitEntropy();
  FTL_CHECK(RAND_bytes(static_cast<uint8_t*>(buffer), size) == 1);
}

uint64_t RandUint64() {
  uint64_t result;
  RandBytes(&result, sizeof(result));
  return result;
}

}  // namespace glue
