// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include "imx227.h"

namespace camera {

// |ZX_PROTOCOL_CAMERA_SENSOR2|

zx_status_t Imx227Device::CameraSensor2Init() {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

void Imx227Device::CameraSensor2DeInit() { FX_NOTIMPLEMENTED(); }

zx_status_t Imx227Device::CameraSensor2GetSensorId(uint32_t* out_id) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetAvailableModes(operating_mode_t* out_modes_list,
                                                         size_t modes_count,
                                                         size_t* out_modes_actual) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetMode(uint32_t mode) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2StartStreaming() {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

void Imx227Device::CameraSensor2StopStreaming() { FX_NOTIMPLEMENTED(); }

zx_status_t Imx227Device::CameraSensor2GetAnalogGain(float* out_gain) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetAnalogGain(float gain, float* out_gain) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetDigitalGain(float* out_gain) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetDigitalGain(float gain, float* out_gain) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetIntegrationTime(float* out_int_time) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetIntegrationTime(float int_time, float* out_int_time) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2Update() {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetOtpSize(uint32_t* out_size) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetOtpData(uint32_t byte_count, uint32_t offset,
                                                  const uint8_t* buf_list, size_t buf_count) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetTestPatternMode(uint16_t* out_value) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetTestPatternMode(uint16_t mode) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetTestPatternData(color_val_t* out_data) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetTestPatternData(const color_val_t* data) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetTestCursorData(rect_vals_t* out_data) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetTestCursorData(const rect_vals_t* data) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetExtensionValue(uint64_t id,
                                                         extension_value_data_type_t** out_value) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetExtensionValue(uint64_t id,
                                                         const extension_value_data_type_t* value,
                                                         extension_value_data_type_t** out_value) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace camera
