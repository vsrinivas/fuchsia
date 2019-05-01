// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_ARM64_PL031_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_ARM64_PL031_H_

#include <mutex>

#include "src/virtualization/bin/vmm/io.h"
#include "src/virtualization/bin/vmm/platform_device.h"

class Guest;

// Implements the PL031 RTC.
class Pl031 : public IoHandler, public PlatformDevice {
 public:
  zx_status_t Init(Guest* guest);

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  // PlatformDevice interface.
  zx_status_t ConfigureDtb(void* dtb) const override;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_ARM64_PL031_H_
