// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_GRAPHICS_DRIVERS_MSD_IMG_RGX_INCLUDE_IMG_SYS_DEVICE_H_
#define SRC_GRAPHICS_DRIVERS_MSD_IMG_RGX_INCLUDE_IMG_SYS_DEVICE_H_

#include <zircon/types.h>

class ImgSysDevice {
 public:
  virtual zx_status_t PowerUp() = 0;
  virtual zx_status_t PowerDown() = 0;
  virtual void* device() = 0;
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_IMG_RGX_INCLUDE_IMG_SYS_DEVICE_H_
