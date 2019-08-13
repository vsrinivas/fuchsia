// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-sensor-device.h"


namespace camera {

void MockSensorDevice::DdkRelease() {
  delete this;
}

zx_status_t MockSensorDevice::CameraSensorInit() {
  if (is_initialized_) {
    return ZX_ERR_BAD_STATE;
  }
  is_initialized_ = true;
  return ZX_OK;
}

void MockSensorDevice::CameraSensorDeInit() {
  is_initialized_ = false;
}

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

zx_status_t MockSensorDevice::CameraSensorSetIntegrationTime(
    int32_t integration_time) {
  integration_time_ = integration_time;
  return ZX_OK;
}

zx_status_t MockSensorDevice::CameraSensorGetInfo(sensor_info_t* out_info) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockSensorDevice::CameraSensorGetSupportedModes(
    sensor_mode_t* out_modes_list, size_t modes_count,
    size_t* out_modes_actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockSensorDevice::SetSensorInfo(const sensor_info_t& info) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t MockSensorDevice::AddMode(const sensor_mode_t& mode) {
  supported_modes_.push_back(mode);
  return ZX_OK;
}


}  // namespace camera
