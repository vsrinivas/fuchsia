// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gdc/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>

#include <memory>

#include <ddktl/device.h>

#include "src/devices/bus/drivers/platform/test/test-gdc-bind.h"
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

static bool isBufferCollectionValid(const buffer_collection_info_2_t* buffer_collection,
                                    const image_format_2_t* image_format) {
  return !(image_format->display_width != kWidth || image_format->display_height != kHeight ||
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
  zx_status_t GdcInitTask(const buffer_collection_info_2_t* input_buffer_collection,
                          const buffer_collection_info_2_t* output_buffer_collection,
                          const image_format_2_t* input_image_format,
                          const image_format_2_t* output_image_format, zx::vmo config_vmo,
                          const hw_accel_callback_t* callback, uint32_t* out_task_index) {
    if (input_buffer_collection == nullptr || output_buffer_collection == nullptr ||
        callback == nullptr || out_task_index == nullptr || !config_vmo.is_valid() ||
        !isBufferCollectionValid(input_buffer_collection, input_image_format) ||
        !isBufferCollectionValid(output_buffer_collection, output_image_format)) {
      return ZX_ERR_INVALID_ARGS;
    }

    // Validate Buffer Collection VMO handles
    // looping thru buffer_count, since its the same for both buffer collections.
    for (uint32_t i = 0; i < input_buffer_collection->buffer_count; i++) {
      if (input_buffer_collection->buffers[i].vmo == ZX_HANDLE_INVALID ||
          output_buffer_collection->buffers[i].vmo == ZX_HANDLE_INVALID ||
          input_buffer_collection->settings.buffer_settings.size_bytes != kVmoSize ||
          output_buffer_collection->settings.buffer_settings.size_bytes != kVmoSize) {
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
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();
};

zx_status_t TestGdcDevice::Create(zx_device_t* parent) {
  auto dev = std::make_unique<TestGdcDevice>(parent);

  zxlogf(INFO, "TestGdcDevice::Create: %s ", DRIVER_NAME);

  auto status = dev->DdkAdd("test-gdc");
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: DdkAdd failed: %d", __func__, status);
    return status;
  }
  // devmgr is now in charge of dev.
  __UNUSED auto ptr = dev.release();

  return ZX_OK;
}

void TestGdcDevice::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void TestGdcDevice::DdkRelease() { delete this; }

zx_status_t test_gdc_bind(void* ctx, zx_device_t* parent) { return TestGdcDevice::Create(parent); }

constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t driver_ops = {};
  driver_ops.version = DRIVER_OPS_VERSION;
  driver_ops.bind = test_gdc_bind;
  return driver_ops;
}();

}  // namespace gdc

ZIRCON_DRIVER(test_gdc, gdc::driver_ops, "zircon", "0.1");
