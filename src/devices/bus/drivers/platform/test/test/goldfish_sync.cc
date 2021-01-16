// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/goldfish/sync/cpp/banjo.h>
#include <lib/zx/event.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <unordered_set>

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-goldfish-sync-bind.h"

#define DRIVER_NAME "test-goldfish-sync"

namespace goldfish {

namespace sync {

namespace {

template <typename T>
zx_status_t CheckHandle(const T& object) {
  if (!object.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }

  zx_info_handle_basic_t handle_info;
  zx_status_t status =
      object.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  if (handle_info.type != T::TYPE) {
    return ZX_ERR_WRONG_TYPE;
  }
  return ZX_OK;
}

}  // namespace

class TestGoldfishSyncDevice;
using DeviceType = ddk::Device<TestGoldfishSyncDevice>;

class TestGoldfishSyncDevice
    : public DeviceType,
      public ddk::GoldfishSyncProtocol<TestGoldfishSyncDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestGoldfishSyncDevice(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Create(std::unique_ptr<TestGoldfishSyncDevice>* out);

  // Methods required by the ddk mixins
  void DdkRelease();

  // |ddk.protocol.goldfish.sync|
  zx_status_t GoldfishSyncCreateTimeline(zx::channel connection);

 private:
  int32_t next_id_ = 0u;
  std::unordered_set<int32_t> ids_;
};

zx_status_t TestGoldfishSyncDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestGoldfishSyncDevice>(parent);

  zxlogf(INFO, "TestGoldfishSyncDevice::Create: %s", DRIVER_NAME);

  auto status = dev->DdkAdd(DRIVER_NAME);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestGoldfishSyncDevice::DdkRelease() { delete this; }

zx_status_t TestGoldfishSyncDevice::GoldfishSyncCreateTimeline(zx::channel request) {
  zxlogf(INFO, "TestGoldfishSyncDevice::%s connection = %u", __func__, request.get());

  return CheckHandle(request);
}

zx_status_t test_goldfish_sync_bind(void* ctx, zx_device_t* parent) {
  return TestGoldfishSyncDevice::Create(parent);
}

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_goldfish_sync_bind;
  return driver_ops;
}();

}  // namespace sync

}  // namespace goldfish

ZIRCON_DRIVER(test_goldfish_sync, goldfish::sync::driver_ops, "zircon", "0.1");
