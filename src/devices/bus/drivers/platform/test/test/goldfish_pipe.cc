// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/zx/event.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <memory>
#include <unordered_set>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-goldfish-pipe-bind.h"

#define DRIVER_NAME "test-goldfish-pipe"
#define GOLDFISH_TEST_HEAP (0x100000000000fffful)

namespace goldfish {

namespace pipe {

namespace {

zx_status_t CheckHandle(const zx::object_base& object, zx_obj_type_t obj_type) {
  if (!object.is_valid()) {
    return ZX_ERR_BAD_HANDLE;
  }

  zx_info_handle_basic_t handle_info;
  zx_status_t status =
      object.get_info(ZX_INFO_HANDLE_BASIC, &handle_info, sizeof(handle_info), nullptr, nullptr);
  if (status != ZX_OK) {
    return status;
  }

  if (handle_info.type != obj_type) {
    return ZX_ERR_WRONG_TYPE;
  }
  return ZX_OK;
}

}  // namespace

class TestGoldfishPipeDevice;
using DeviceType = ddk::Device<TestGoldfishPipeDevice>;

class TestGoldfishPipeDevice
    : public DeviceType,
      public ddk::GoldfishPipeProtocol<TestGoldfishPipeDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestGoldfishPipeDevice(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Create(std::unique_ptr<TestGoldfishPipeDevice>* out);

  // Methods required by the ddk mixins
  void DdkRelease();

  // |fuchsia.hardware.goldfish.pipe.GoldfishPipe|
  zx_status_t GoldfishPipeCreate(int32_t* out_id, zx::vmo* out_vmo);
  zx_status_t GoldfishPipeSetEvent(int32_t id, zx::event pipe_event);
  void GoldfishPipeDestroy(int32_t id);
  void GoldfishPipeOpen(int32_t id);
  void GoldfishPipeExec(int32_t id);
  zx_status_t GoldfishPipeGetBti(zx::bti* out_bti);
  zx_status_t GoldfishPipeConnectSysmem(zx::channel connection);
  zx_status_t GoldfishPipeRegisterSysmemHeap(uint64_t heap, zx::channel connection);

 private:
  int32_t next_id_ = 0u;
  std::unordered_set<int32_t> ids_;
};

zx_status_t TestGoldfishPipeDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestGoldfishPipeDevice>(parent);

  zxlogf(INFO, "TestGoldfishPipeDevice::Create: %s", DRIVER_NAME);

  auto status = dev->DdkAdd(DRIVER_NAME);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }

  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestGoldfishPipeDevice::DdkRelease() { delete this; }

zx_status_t TestGoldfishPipeDevice::GoldfishPipeCreate(int32_t* out_id, zx::vmo* out_vmo) {
  zxlogf(INFO, "TestGoldfishPipeDevice::%s", __func__);

  zx_status_t status = zx::vmo::create(/*size=*/65536, /*options=*/0u, out_vmo);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s zx_vmo_create failed %d", __func__, status);
    return status;
  }

  *out_id = next_id_++;
  ids_.insert(*out_id);

  return ZX_OK;
}

zx_status_t TestGoldfishPipeDevice::GoldfishPipeSetEvent(int32_t id, zx::event pipe_event) {
  zxlogf(INFO, "TestGoldfishPipeDevice::%s id = %d pipe_event = %u", __func__, id,
         pipe_event.get());

  if (ids_.find(id) == ids_.end()) {
    return ZX_ERR_INVALID_ARGS;
  }

  return CheckHandle(pipe_event, ZX_OBJ_TYPE_EVENT);
}

void TestGoldfishPipeDevice::GoldfishPipeDestroy(int32_t id) {
  zxlogf(INFO, "TestGoldfishPipeDevice::%s id = %d", __func__, id);

  ids_.erase(id);
}

void TestGoldfishPipeDevice::GoldfishPipeOpen(int32_t id) {
  zxlogf(INFO, "TestGoldfishPipeDevice::%s id = %d", __func__, id);

  ZX_ASSERT(ids_.find(id) != ids_.end());
}

void TestGoldfishPipeDevice::GoldfishPipeExec(int32_t id) {
  zxlogf(INFO, "TestGoldfishPipeDevice::%s id = %d", __func__, id);

  ZX_ASSERT(ids_.find(id) != ids_.end());
}

zx_status_t TestGoldfishPipeDevice::GoldfishPipeGetBti(zx::bti* out_bti) {
  zxlogf(INFO, "TestGoldfishPipeDevice::%s", __func__);

  // We don't have a good way to create a BTI on the test board
  // (fake BTIs don't work when crossing process boundaries), so
  // we just return a non-BTI handle for GetBti() method to make
  // this fake device work.
  zx::event dummy_event;
  zx_status_t status = zx::event::create(0u, &dummy_event);
  if (status != ZX_OK) {
    return status;
  }
  out_bti->reset(dummy_event.release());

  return ZX_OK;
}

zx_status_t TestGoldfishPipeDevice::GoldfishPipeConnectSysmem(zx::channel connection) {
  zxlogf(INFO, "TestGoldfishPipeDevice::%s connection = %u", __func__, connection.get());

  return CheckHandle(connection, ZX_OBJ_TYPE_CHANNEL);
}

zx_status_t TestGoldfishPipeDevice::GoldfishPipeRegisterSysmemHeap(uint64_t heap,
                                                                   zx::channel connection) {
  zxlogf(INFO, "TestGoldfishPipeDevice::%s heap = %lu connection = %u", __func__, heap,
         connection.get());

  if (heap != GOLDFISH_TEST_HEAP) {
    return ZX_ERR_INVALID_ARGS;
  }

  return CheckHandle(connection, ZX_OBJ_TYPE_CHANNEL);
}

zx_status_t test_goldfish_pipe_bind(void* ctx, zx_device_t* parent) {
  return TestGoldfishPipeDevice::Create(parent);
}

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_goldfish_pipe_bind;
  return driver_ops;
}();

}  // namespace pipe

}  // namespace goldfish

ZIRCON_DRIVER(test_goldfish_pipe, goldfish::pipe::driver_ops, "zircon", "0.1");
