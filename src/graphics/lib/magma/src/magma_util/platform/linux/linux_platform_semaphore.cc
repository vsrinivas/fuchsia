// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "platform_semaphore.h"

namespace magma {

class LinuxPlatformSemaphore : public PlatformSemaphore {
 public:
  void Signal() override {}

  void Reset() override {}

  magma::Status WaitNoReset(uint64_t timeout_ms) override {
    return DRET(MAGMA_STATUS_UNIMPLEMENTED);
  }

  magma::Status Wait(uint64_t timeout_ms) override { return DRET(MAGMA_STATUS_UNIMPLEMENTED); }

  bool WaitAsync(PlatformPort* platform_port) override {
    return DRETF(false, "WaitAsync not implemented");
  }
};

std::unique_ptr<PlatformSemaphore> PlatformSemaphore::Create() {
  return DRETP(nullptr, "PlatformSemaphore::Create not supported");
}

std::unique_ptr<PlatformSemaphore> PlatformSemaphore::Import(uint32_t handle) {
  return DRETP(nullptr, "PlatformSemaphore::Import not supported");
}

}  // namespace magma
