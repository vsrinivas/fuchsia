// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_DRIVER_H_
#define SRC_GRAPHICS_LIB_MAGMA_SRC_SYS_DRIVER_MAGMA_DRIVER_H_

#include <lib/fit/function.h>

#include "magma_system_device.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd.h"

using msd_driver_unique_ptr_t = std::unique_ptr<msd_driver_t, fit::function<void(msd_driver_t*)>>;

static inline msd_driver_unique_ptr_t MsdDriverUniquePtr(msd_driver_t* driver) {
  return msd_driver_unique_ptr_t(driver, &msd_driver_destroy);
}

class MagmaDriver {
 public:
  MagmaDriver(msd_driver_unique_ptr_t msd_drv) : msd_drv_(std::move(msd_drv)) {}

  std::unique_ptr<MagmaSystemDevice> CreateDevice(void* device) {
    msd_device_t* msd_dev = msd_driver_create_device(msd_drv_.get(), device);
    if (!msd_dev)
      return DRETP(nullptr, "msd_create_device failed");

    return MagmaSystemDevice::Create(MsdDeviceUniquePtr(msd_dev));
  }

  static std::unique_ptr<MagmaDriver> Create() {
    msd_driver_t* msd_drv = msd_driver_create();
    if (!msd_drv)
      return DRETP(nullptr, "msd_create returned null");

    return std::make_unique<MagmaDriver>(MsdDriverUniquePtr(msd_drv));
  }

  uint32_t DuplicateInspectVmo() { return msd_driver_duplicate_inspect_handle(msd_drv_.get()); }

 private:
  msd_driver_unique_ptr_t msd_drv_;
};

#endif  // MAGMA_DRIVER_H
