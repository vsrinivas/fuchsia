// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/i2cimpl.h>

#include "src/devices/bus/drivers/platform/test/test-i2c-bind.h"

#define DRIVER_NAME "test-i2c"

namespace i2c {

class TestI2cDevice;
using DeviceType = ddk::Device<TestI2cDevice>;

class TestI2cDevice : public DeviceType,
                      public ddk::I2cImplProtocol<TestI2cDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestI2cDevice(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Create(std::unique_ptr<TestI2cDevice>* out);

  uint32_t I2cImplGetBusCount();
  zx_status_t I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size);
  zx_status_t I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate);
  zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count);

  // Methods required by the ddk mixins
  void DdkRelease();
};

zx_status_t TestI2cDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestI2cDevice>(parent);
  pdev_protocol_t pdev;
  zx_status_t status;

  zxlogf(INFO, "TestI2cDevice::Create: %s ", DRIVER_NAME);

  status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_PDEV", __func__);
    return status;
  }

  status = dev->DdkAdd("test-i2c");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }
  // devmgr is now in charge of dev.
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestI2cDevice::DdkRelease() { delete this; }

uint32_t TestI2cDevice::I2cImplGetBusCount() { return 2; }
zx_status_t TestI2cDevice::I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size) {
  *out_size = 1024;
  return ZX_OK;
}

zx_status_t TestI2cDevice::I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t TestI2cDevice::I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list,
                                           size_t op_count) {
  // We only support write/read transactions.
  if (op_count != 2) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (op_list[0].is_read || !op_list[1].is_read) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  if (op_list[0].data_size != op_list[1].data_size) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Reverse the digits.
  auto* src = static_cast<const uint32_t*>(op_list[0].data_buffer);
  auto* dest = static_cast<uint32_t*>(op_list[1].data_buffer);
  size_t count = op_list[0].data_size / sizeof(uint32_t);
  for (size_t i = 0; i < count; i++) {
    dest[i] = src[count - i - 1];
  }

  return ZX_OK;
}

zx_status_t test_i2c_bind(void* ctx, zx_device_t* parent) { return TestI2cDevice::Create(parent); }

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_i2c_bind;
  return driver_ops;
}();

}  // namespace i2c

ZIRCON_DRIVER(test_i2c, i2c::driver_ops, "zircon", "0.1")
