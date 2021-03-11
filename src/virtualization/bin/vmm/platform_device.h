// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_PLATFORM_DEVICE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_PLATFORM_DEVICE_H_

#include <lib/stdcompat/span.h>
#include <zircon/types.h>

#include <cstddef>

class PlatformDevice {
 public:
  virtual ~PlatformDevice() = default;

  virtual zx_status_t ConfigureZbi(cpp20::span<std::byte> zbi) const { return ZX_OK; }
  virtual zx_status_t ConfigureDtb(void *dtb) const { return ZX_OK; }
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_PLATFORM_DEVICE_H_
