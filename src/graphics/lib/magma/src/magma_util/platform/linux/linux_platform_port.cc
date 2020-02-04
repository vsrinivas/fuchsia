// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "platform_port.h"

namespace magma {

class LinuxPlatformPort : public PlatformPort {
 public:
  void Close() override {}

  Status Wait(uint64_t* key_out, uint64_t timeout_ms) override {
    return DRET(MAGMA_STATUS_UNIMPLEMENTED);
  }
};

std::unique_ptr<PlatformPort> PlatformPort::Create() {
  return DRETP(nullptr, "PlatformPort::Create not supported");
}

}  // namespace magma
