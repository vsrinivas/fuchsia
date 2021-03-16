// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/goldfish/addressspace/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-goldfish-address-space-bind.h"

#define DRIVER_NAME "test-goldfish-address-space"

namespace goldfish {

namespace address_space {

class TestGoldfishAddressSpaceDevice;
using DeviceType = ddk::Device<TestGoldfishAddressSpaceDevice>;

class TestGoldfishAddressSpaceDevice
    : public DeviceType,
      public ddk::GoldfishAddressSpaceProtocol<TestGoldfishAddressSpaceDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestGoldfishAddressSpaceDevice(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Create(std::unique_ptr<TestGoldfishAddressSpaceDevice>* out);

  // Methods required by the ddk mixins
  void DdkRelease();

  // |fuchsia.hardware.goldfish.addressspace.GoldfishAddressSpace|
  zx_status_t GoldfishAddressSpaceOpenChildDriver(address_space_child_driver_type_t type,
                                                  zx::channel request);
};

zx_status_t TestGoldfishAddressSpaceDevice::GoldfishAddressSpaceOpenChildDriver(
    address_space_child_driver_type_t type, zx::channel request) {
  zxlogf(INFO,
         "TestGoldfishAddressSpaceDevice::GoldfishAddressSpace.OpenChildDriver "
         "type = %u, request = %u",
         type, request.get());
  if (type != ADDRESS_SPACE_CHILD_DRIVER_TYPE_DEFAULT) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (!request.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }

  zx_info_handle_basic_t handle_info;
  zx_status_t status =
      request.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  if (handle_info.type != ZX_OBJ_TYPE_CHANNEL) {
    return ZX_ERR_WRONG_TYPE;
  }
  return ZX_OK;
}

zx_status_t TestGoldfishAddressSpaceDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestGoldfishAddressSpaceDevice>(parent);

  zxlogf(INFO, "TestGoldfishAddressSpaceDevice::Create: %s", DRIVER_NAME);

  auto status = dev->DdkAdd(DRIVER_NAME);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestGoldfishAddressSpaceDevice::DdkRelease() { delete this; }

zx_status_t test_goldfish_address_space_bind(void* ctx, zx_device_t* parent) {
  return TestGoldfishAddressSpaceDevice::Create(parent);
}

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_goldfish_address_space_bind;
  return driver_ops;
}();

}  // namespace address_space

}  // namespace goldfish

ZIRCON_DRIVER(test_goldfish_address_space, goldfish::address_space::driver_ops, "zircon", "0.1");
