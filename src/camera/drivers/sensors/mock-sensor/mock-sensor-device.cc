// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-sensor-device.h"

#include <ddk/binding.h>
#include <ddk/debug.h>

namespace camera {

zx_status_t MockSensorDevice::Create(void* ctx, zx_device_t* parent) {
  auto device = std::make_unique<MockSensorDevice>(parent);

  zx_status_t status = device->DdkAdd("mock-sensor");
  if (status != ZX_OK) {
    zxlogf(ERROR, "mock-sensor-device: Could not add mock-sensor: %d\n", status);
    return status;
  }
  zxlogf(ERROR, "mock-sensor-device: Successfully added mock sensor: %d\n", status);
  // Device is held by DevMgr.
  __UNUSED auto* dev = device.release();
  return ZX_OK;
}

void MockSensorDevice::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void MockSensorDevice::DdkRelease() { delete this; }

zx_status_t MockSensorDevice::CameraSensorInit() {
  if (is_initialized_) {
    return ZX_ERR_BAD_STATE;
  }
  is_initialized_ = true;
  return ZX_OK;
}

void MockSensorDevice::CameraSensorDeInit() { is_initialized_ = false; }

zx_status_t MockSensorDevice::CameraSensorSetMode(uint8_t mode) {
  if (mode >= supported_modes_.size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  mode_ = mode;
  return ZX_OK;
}

zx_status_t MockSensorDevice::CameraSensorStartStreaming() {
  if (is_streaming_) {
    return ZX_ERR_BAD_STATE;
  }
  is_streaming_ = true;
  return ZX_OK;
}

zx_status_t MockSensorDevice::CameraSensorStopStreaming() {
  if (!is_streaming_) {
    return ZX_ERR_BAD_STATE;
  }
  is_streaming_ = false;
  return ZX_OK;
}

int32_t MockSensorDevice::CameraSensorSetAnalogGain(int32_t gain) {
  analog_gain_ = gain;
  return analog_gain_;
}

int32_t MockSensorDevice::CameraSensorSetDigitalGain(int32_t gain) {
  digital_gain_ = gain;
  return digital_gain_;
}

zx_status_t MockSensorDevice::CameraSensorSetIntegrationTime(int32_t integration_time) {
  integration_time_ = integration_time;
  return ZX_OK;
}

zx_status_t MockSensorDevice::CameraSensorGetInfo(camera_sensor_info_t* out_info) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockSensorDevice::CameraSensorGetSupportedModes(camera_sensor_mode_t* out_modes_list,
                                                            size_t modes_count,
                                                            size_t* out_modes_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockSensorDevice::SetSensorInfo(const camera_sensor_info_t& info) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockSensorDevice::AddMode(const camera_sensor_mode_t& mode) {
  supported_modes_.push_back(mode);
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = MockSensorDevice::Create;
  return ops;
}();

}  // namespace camera

// clang-format off
ZIRCON_DRIVER_BEGIN(mock-sensor, camera::driver_ops, "mock-sensor", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_VCAMERA_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_CAMERA_SENSOR)
ZIRCON_DRIVER_END(mock-sensor)
    // clang-format on
