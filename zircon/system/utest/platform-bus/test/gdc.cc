// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gdc.h>

#include "zircon/errors.h"

#define DRIVER_NAME "test-gdc"

namespace gdc {

namespace {
constexpr uint32_t kWidth = 1080;
constexpr uint32_t kHeight = 764;
constexpr uint32_t kNumBuffers = 10;
constexpr uint32_t kTaskId = 123;
constexpr uint32_t kVmoSize = 0x1000;
constexpr uint32_t kBufferId = 777;
}  // namespace

static bool isBufferCollectionValid(const buffer_collection_info_t* buffer_collection) {
  return !(buffer_collection->format.image.width != kWidth ||
           buffer_collection->format.image.height != kHeight ||
           buffer_collection->buffer_count != kNumBuffers);
}

class TestGdcDevice;
using DeviceType = ddk::Device<TestGdcDevice, ddk::Unbindable>;

class TestGdcDevice : public DeviceType,
                      public ddk::GdcProtocol<TestGdcDevice, ddk::base_protocol> {
 public:
  static zx_status_t Create(zx_device_t* parent);

  explicit TestGdcDevice(zx_device_t* parent) : DeviceType(parent) {}

  zx_status_t Create(std::unique_ptr<TestGdcDevice>* out);

  // ZX_PROTOCOL_GDC (Refer to gdc.banjo for documentation).
  zx_status_t GdcInitTask(const buffer_collection_info_t* input_buffer_collection,
                          const buffer_collection_info_t* output_buffer_collection,
                          zx::vmo config_vmo, const gdc_callback_t* callback,
                          uint32_t* out_task_index) {
    if (input_buffer_collection == nullptr || output_buffer_collection == nullptr ||
        callback == nullptr || out_task_index == nullptr || !config_vmo.is_valid() ||
        !isBufferCollectionValid(input_buffer_collection) ||
        !isBufferCollectionValid(output_buffer_collection)) {
      return ZX_ERR_INVALID_ARGS;
    }

    // Validate Buffer Collection VMO handles
    // looping thru buffer_count, since its the same for both buffer collections.
    for (uint32_t i = 0; i < input_buffer_collection->buffer_count; i++) {
      if (input_buffer_collection->vmos[i] == ZX_HANDLE_INVALID ||
          output_buffer_collection->vmos[i] == ZX_HANDLE_INVALID ||
          input_buffer_collection->vmo_size != kVmoSize ||
          output_buffer_collection->vmo_size != kVmoSize) {
        return ZX_ERR_INVALID_ARGS;
      }
    }

    *out_task_index = kTaskId;
    return ZX_OK;
  }

  zx_status_t GdcProcessFrame(uint32_t task_index, uint32_t input_buffer_index) {
    if (task_index != kTaskId || input_buffer_index != kBufferId) {
      return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
  }

  void GdcRemoveTask(uint32_t task_index) { ZX_ASSERT(task_index == kTaskId); }

  void GdcReleaseFrame(uint32_t task_index, uint32_t buffer_index) {
    ZX_ASSERT(task_index == kTaskId);
    ZX_ASSERT(buffer_index == kBufferId);
  }

  // Methods required by the ddk mixins
  void DdkUnbind();
  void DdkRelease();
};

zx_status_t TestGdcDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestGdcDevice>(parent);

  zxlogf(INFO, "TestGdcDevice::Create: %s \n", DRIVER_NAME);

  auto status = dev->DdkAdd("test-gdc");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d\n", __func__, status);
    return status;
  }
  // devmgr is now in charge of dev.
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestGdcDevice::DdkUnbind() { DdkRemove(); }

void TestGdcDevice::DdkRelease() { delete this; }

zx_status_t test_gdc_bind(void* ctx, zx_device_t* parent) { return TestGdcDevice::Create(parent); }

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_gdc_bind;
  return driver_ops;
}();

}  // namespace gdc

// clang-format off
ZIRCON_DRIVER_BEGIN(test_gdc, gdc::driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_GDC),
ZIRCON_DRIVER_END(test_gdc)
