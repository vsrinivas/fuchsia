// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "imx227.h"
#include "src/camera/drivers/sensors/imx227/constants.h"
#include "src/camera/drivers/sensors/imx227/imx227_id.h"
#include "src/camera/drivers/sensors/imx227/imx227_modes.h"
#include "src/camera/drivers/sensors/imx227/imx227_otp_config.h"
#include "src/camera/drivers/sensors/imx227/imx227_seq.h"
#include "src/camera/drivers/sensors/imx227/mipi_ccs_regs.h"

namespace camera {

namespace {
// Extension Values
const int32_t kLog2GainShift = 18;
const int32_t kSensorExpNumber = 1;
const uint32_t kMasterClock = 288000000;

const uint32_t kDefaultMaxIntegrationTimeInLines = kMaxCoarseIntegrationTimeFor15fpsInLines;
const uint16_t kEndOfSequence = 0x0000;
}  // namespace

// Gets the register value from the sequence table.
// |id| : Index of the sequence table.
// |address| : Address of the register.
static fit::result<uint8_t, zx_status_t> GetRegisterValueFromSequence(uint8_t index,
                                                                      uint16_t address) {
  if (index >= kSEQUENCE_TABLE.size()) {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }
  const InitSeqFmt* sequence = kSEQUENCE_TABLE[index];
  while (true) {
    uint16_t register_address = sequence->address;
    uint16_t register_value = sequence->value;
    uint16_t register_len = sequence->len;
    if (register_address == kEndOfSequence && register_value == 0 && register_len == 0) {
      break;
    }
    if (address == register_address) {
      return fit::ok(register_value);
    }
    sequence++;
  }
  return fit::error(ZX_ERR_NOT_FOUND);
}

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

  is_streaming_ = false;
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
  std::lock_guard guard(lock_);
  if (modes_count > available_modes.size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (size_t i = 0; i < available_modes.size(); i++) {
    out_modes_list[i] = available_modes[i];
  }
  *out_modes_actual = available_modes.size();
  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2SetMode(uint32_t mode) {
  std::lock_guard guard(lock_);

  HwInit();

  if (mode > num_modes_) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!ValidateSensorID()) {
    return ZX_ERR_INTERNAL;
  }
  InitSensor(available_modes[mode].idx);
  InitMipiCsi(mode);
  current_mode_ = mode;
  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2StartStreaming() {
  if (is_streaming_) {
    return ZX_OK;
  }

  std::lock_guard guard(lock_);
  if (!IsSensorInitialized()) {
    return ZX_ERR_BAD_STATE;
  }
  zxlogf(DEBUG, "%s Camera Sensor Start Streaming", __func__);
  is_streaming_ = true;
  Write8(kModeSelectReg, 0x01);
  return ZX_OK;
}

void Imx227Device::CameraSensor2StopStreaming() {
  if (!IsSensorInitialized() || !is_streaming_) {
    return;
  }

  std::lock_guard guard(lock_);
  is_streaming_ = false;
  Write8(kModeSelectReg, 0x00);
  HwDeInit();
}

zx_status_t Imx227Device::CameraSensor2GetAnalogGain(float* out_gain) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetAnalogGain(float gain, float* out_gain) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetDigitalGain(float* out_gain) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetDigitalGain(float gain, float* out_gain) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetIntegrationTime(float* out_int_time) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetIntegrationTime(float int_time, float* out_int_time) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2Update() { return ZX_ERR_NOT_SUPPORTED; }

zx_status_t Imx227Device::CameraSensor2GetOtpSize(uint32_t* out_size) {
  *out_size = OTP_TOTAL_SIZE;
  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2GetOtpData(uint32_t byte_count, uint32_t offset,
                                                  const uint8_t* buf_list, size_t buf_count) {
  if ((byte_count + offset) > OTP_TOTAL_SIZE) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  if (buf_count < byte_count) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  auto result = OtpRead();
  if (result.is_error()) {
    return result.error();
  }
  auto vmo = result.take_value();
  if (!OtpValidate(vmo)) {
    zxlogf(ERROR, "%s; OTP validation failed.", __func__);
    return ZX_ERR_INTERNAL;
  }
  zx_status_t status =
      vmo.read(static_cast<void*>(const_cast<uint8_t*>(buf_list)), offset, byte_count);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s; Reading from VMO failed.", __func__);
  }
  return status;
}

zx_status_t Imx227Device::CameraSensor2GetTestPatternMode(uint16_t* out_value) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetTestPatternMode(uint16_t mode) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetTestPatternData(color_val_t* out_data) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetTestPatternData(const color_val_t* data) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetTestCursorData(rect_t* out_data) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2SetTestCursorData(const rect_t* data) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t Imx227Device::CameraSensor2GetExtensionValue(uint64_t id,
                                                         extension_value_data_type_t* out_value) {
  std::lock_guard guard(lock_);

  switch (id) {
    case TOTAL_RESOLUTION: {
      auto hmax_result =
          GetRegisterValueFromSequence(available_modes[current_mode_].idx, kLineLengthPckReg);
      auto vmax_result =
          GetRegisterValueFromSequence(available_modes[current_mode_].idx, kFrameLengthLinesReg);
      if (hmax_result.is_error() || vmax_result.is_error()) {
        return ZX_ERR_INTERNAL;
      }
      out_value->dimension_value = dimensions_t{
          .x = static_cast<float>(hmax_result.value()),
          .y = static_cast<float>(vmax_result.value()),
      };
      break;
    }
    case ACTIVE_RESOLUTION:
      out_value->dimension_value = available_modes[current_mode_].resolution_in;
      break;
    case PIXELS_PER_LINE: {
      auto hmax_result =
          GetRegisterValueFromSequence(available_modes[current_mode_].idx, kLineLengthPckReg);
      if (hmax_result.is_error()) {
        return ZX_ERR_INTERNAL;
      }
      out_value->uint_value = hmax_result.value();
      break;
    }
    case AGAIN_LOG2_MAX:
    case DGAIN_LOG2_MAX:
      out_value->int_value = 3 << kLog2GainShift;
      break;
    case AGAIN_ACCURACY:
      out_value->int_value = 1 << kLog2GainShift;
      break;
    case INT_TIME_MIN:
      out_value->uint_value = 1;
      break;
    case INT_TIME_MAX:
    case INT_TIME_LONG_MAX:
    case INT_TIME_LIMIT:
      out_value->uint_value = kDefaultMaxIntegrationTimeInLines;
      break;
    case DAY_LIGHT_INT_TIME_MAX:
      out_value->uint_value = 0;
      break;
    case INT_TIME_APPLY_DELAY:
      out_value->int_value = 2;
      break;
    case ISP_EXPOSURE_CHANNEL_DELAY:
      out_value->int_value = 0;
      break;
    case X_OFFSET:
    case Y_OFFSET:
      out_value->int_value = 0;
      break;
    case LINES_PER_SECOND: {
      auto hmax_result =
          GetRegisterValueFromSequence(available_modes[current_mode_].idx, kLineLengthPckReg);
      if (hmax_result.is_error()) {
        return ZX_ERR_INTERNAL;
      }
      out_value->uint_value = kMasterClock / hmax_result.value();
      break;
    }
    case SENSOR_EXP_NUMBER:
      out_value->int_value = kSensorExpNumber;
      break;
    case MODE:
      out_value->uint_value = current_mode_;
      break;
    case FRAME_RATE_COARSE_INT_LUT:
      std::copy(std::begin(frame_rate_to_integration_time_lut),
                std::end(frame_rate_to_integration_time_lut), out_value->frame_rate_info_value);
      break;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2SetExtensionValue(uint64_t id,
                                                         const extension_value_data_type_t* value,
                                                         extension_value_data_type_t* out_value) {
  return ZX_ERR_NOT_SUPPORTED;
}

}  // namespace camera
