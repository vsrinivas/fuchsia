// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_driver.h"

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_arm_device.h"

MsdArmDriver::MsdArmDriver() { magic_ = kMagic; }

std::unique_ptr<MsdArmDriver> MsdArmDriver::Create() {
  return std::unique_ptr<MsdArmDriver>(new MsdArmDriver());
}

void MsdArmDriver::Destroy(MsdArmDriver* drv) { delete drv; }

//////////////////////////////////////////////////////////////////////////////

msd_driver_t* msd_driver_create(void) { return MsdArmDriver::Create().release(); }

void msd_driver_configure(struct msd_driver_t* drv, uint32_t flags) {
  MsdArmDriver::cast(drv)->configure(flags);
}

void msd_driver_destroy(msd_driver_t* drv) { MsdArmDriver::Destroy(MsdArmDriver::cast(drv)); }

msd_device_t* msd_driver_create_device(msd_driver_t* drv, void* device_handle) {
  bool start_device_thread =
      (MsdArmDriver::cast(drv)->configure_flags() & MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD) == 0;

  std::unique_ptr<MsdArmDevice> device = MsdArmDevice::Create(device_handle, start_device_thread);
  if (!device)
    return DRETP(nullptr, "failed to create device");

  // Transfer ownership across the ABI
  return device.release();
}
