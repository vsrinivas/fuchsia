// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msd_arm_driver.h"

#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "msd_arm_device.h"

MsdArmDriver::MsdArmDriver() {
  magic_ = kMagic;
  root_node_ = inspector_.GetRoot().CreateChild("msd-arm-mali");
}

std::unique_ptr<MsdArmDriver> MsdArmDriver::Create() {
  return std::unique_ptr<MsdArmDriver>(new MsdArmDriver());
}

void MsdArmDriver::Destroy(MsdArmDriver* drv) { delete drv; }

uint32_t MsdArmDriver::DuplicateInspectHandle() { return inspector_.DuplicateVmo().release(); }

std::unique_ptr<MsdArmDevice> MsdArmDriver::CreateDevice(void* device_handle) {
  bool start_device_thread = (configure_flags() & MSD_DRIVER_CONFIG_TEST_NO_DEVICE_THREAD) == 0;

  std::unique_ptr<MsdArmDevice> device =
      MsdArmDevice::Create(device_handle, start_device_thread, &root_node());
  if (!device)
    return DRETP(nullptr, "failed to create device");
  return device;
}

std::unique_ptr<MsdArmDevice> MsdArmDriver::CreateDeviceForTesting(
    std::unique_ptr<magma::PlatformDevice> platform_device,
    std::unique_ptr<magma::PlatformBusMapper> bus_mapper) {
  auto device = std::make_unique<MsdArmDevice>();
  device->set_inspect(root_node_.CreateChild("device"));
  if (!device->Init(std::move(platform_device), std::move(bus_mapper)))
    return DRETF(nullptr, "Failed to create device");
  return device;
}

//////////////////////////////////////////////////////////////////////////////

msd_driver_t* msd_driver_create(void) { return MsdArmDriver::Create().release(); }

void msd_driver_configure(struct msd_driver_t* drv, uint32_t flags) {
  MsdArmDriver::cast(drv)->configure(flags);
}

void msd_driver_destroy(msd_driver_t* drv) { MsdArmDriver::Destroy(MsdArmDriver::cast(drv)); }

uint32_t msd_driver_duplicate_inspect_handle(struct msd_driver_t* drv) {
  return MsdArmDriver::cast(drv)->DuplicateInspectHandle();
}

msd_device_t* msd_driver_create_device(msd_driver_t* drv, void* device_handle) {
  auto arm_drv = MsdArmDriver::cast(drv);
  auto device = arm_drv->CreateDevice(device_handle);
  // Transfer ownership across the ABI
  return device.release();
}
