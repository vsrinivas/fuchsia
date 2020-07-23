// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <ddk/protocol/camerasensor.h>
#include <ddk/debug.h>

#include "ddk/protocol/camera.h"
#include "imx227.h"
#include "src/camera/drivers/sensors/imx227/imx227_guid.h"
#include "src/camera/drivers/sensors/imx227/imx227_seq.h"

namespace camera {

namespace {
// Extension Values
const int32_t kLog2GainShift = 18;
const int32_t kSensorExpNumber = 1;
const uint32_t kMasterClock = 288000000;
}  // namespace

// |ZX_PROTOCOL_CAMERA_SENSOR2|

zx_status_t Imx227Device::CameraSensor2Init() {
  std::lock_guard guard(lock_);

  HwInit();

  initialized_ = true;
  return ZX_OK;
}

void Imx227Device::CameraSensor2DeInit() {
  std::lock_guard guard(lock_);
  mipi_.DeInit();
  HwDeInit();
  // The reference code has this sleep. It is most likely needed for the clock to stabalize.
  // There is no other way to tell whether the sensor has successfully powered down.
  zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

  ctx_.streaming_flag = false;
  initialized_ = false;
}

zx_status_t Imx227Device::CameraSensor2GetSensorId(uint32_t* out_id) {
  std::lock_guard guard(lock_);
  auto result = Read16(kSensorModelIdReg);
  if (result.is_error()) {
    return result.take_error();
  }
  if (result.value() != kSensorModelIdDefault) {
    zxlogf(ERROR, "Sensor not online, read %d instead", result.value());
    return ZX_ERR_INTERNAL;
  }
  *out_id = result.take_value();
  return ZX_OK;
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
  std::lock_guard guard(lock_);

  switch (id) {
    case TOTAL_RESOLUTION:
      (*out_value)->resolution_value = dimensions_t { .x = static_cast<float>(ctx_.HMAX), .y = static_cast<float>(ctx_.VMAX) };
      break;
    case ACTIVE_RESOLUTION: {
      // TODO(55178): Remove this conversion.
      auto res = supported_modes[mode_].resolution;
      (*out_value)->resolution_value =
          dimensions_t { .x = static_cast<float>(res.width), .y = static_cast<float>(res.height) };
      break;
    }
    case PIXELS_PER_LINE:
      (*out_value)->uint_value = ctx_.HMAX;
      break;
    case AGAIN_LOG2_MAX:
    case DGAIN_LOG2_MAX:
      (*out_value)->int_value = 3 << kLog2GainShift;
      break;
    case AGAIN_ACCURACY:
      (*out_value)->int_value = 1 << kLog2GainShift;
      break;
    case INT_TIME_MIN:
      (*out_value)->uint_value = ctx_.int_time_min;
      break;
    case INT_TIME_MAX:
    case INT_TIME_LONG_MAX:
    case INT_TIME_LIMIT:
      (*out_value)->uint_value = ctx_.int_time_limit;
      break;
    case DAY_LIGHT_INT_TIME_MAX:
      break;
    case INT_TIME_APPLY_DELAY:
      (*out_value)->int_value = 2;
      break;
    case ISP_EXPOSURE_CHANNEL_DELAY:
      (*out_value)->int_value = 0;
      break;
    case X_OFFSET:
    case Y_OFFSET:
      break;
    case LINES_PER_SECOND:
      (*out_value)->uint_value = kMasterClock / ctx_.HMAX;
      break;
    case SENSOR_EXP_NUMBER:
      (*out_value)->int_value = kSensorExpNumber;
      break;
    case MODE:
      (*out_value)->uint_value = mode_;
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2SetExtensionValue(uint64_t id,
                                                         const extension_value_data_type_t* value,
                                                         extension_value_data_type_t** out_value) {
  FX_NOTIMPLEMENTED();
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace camera
