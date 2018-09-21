// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_PLATFORM_DEVICE_H_
#define GARNET_LIB_MACHINA_PLATFORM_DEVICE_H_

#include <zircon/types.h>

namespace machina {

class PlatformDevice {
 public:
  virtual ~PlatformDevice() = default;

  virtual zx_status_t ConfigureZbi(void *zbi_base, size_t zbi_max) const {
    return ZX_OK;
  }
  virtual zx_status_t ConfigureDtb(void *dtb) const {
    return ZX_OK;
  }
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_PLATFORM_DEVICE_H_
