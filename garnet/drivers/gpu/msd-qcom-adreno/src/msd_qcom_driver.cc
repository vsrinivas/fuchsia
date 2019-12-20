// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_qcom_driver.h"

#include <msd.h>

#include <magma_util/macros.h>

#include "msd_qcom_device.h"

msd_driver_t* msd_driver_create(void) { return new MsdQcomDriver(); }

void msd_driver_configure(struct msd_driver_t* drv, uint32_t flags) {}

void msd_driver_destroy(msd_driver_t* drv) { delete MsdQcomDriver::cast(drv); }

msd_device_t* msd_driver_create_device(msd_driver_t* drv, void* device_handle) {
  std::unique_ptr<MsdQcomDevice> device = MsdQcomDevice::Create(device_handle);
  if (!device)
    return DRETP(nullptr, "failed to create device");
  return device.release();
}
