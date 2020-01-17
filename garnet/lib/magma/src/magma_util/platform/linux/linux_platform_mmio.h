// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LINUX_PLATFORM_MMIO_H
#define LINUX_PLATFORM_MMIO_H

#include "platform_mmio.h"

namespace magma {

class LinuxPlatformMmio : public PlatformMmio {
 public:
  LinuxPlatformMmio(void* addr, uint64_t size) : PlatformMmio(addr, size) {}

  uint64_t physical_address() override { return 0; }
};

}  // namespace magma

#endif  // LINUX_PLATFORM_MMIO_H
