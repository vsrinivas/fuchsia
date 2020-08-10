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

// |ZX_PROTOCOL_CAMERA_SENSOR2|

zx_status_t Imx227Device::CameraSensor2Init() {
  std::lock_guard guard(lock_);

  HwInit();
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
  zxlogf(DEBUG, "%s Camera Sensor Start Streaming", __func__);
  is_streaming_ = true;
  Write8(kModeSelectReg, 0x01);
  return ZX_OK;
}

void Imx227Device::CameraSensor2StopStreaming() {
  if (!is_streaming_) {
    return;
  }

  std::lock_guard guard(lock_);
  is_streaming_ = false;
  Write8(kModeSelectReg, 0x00);
  HwDeInit();
}

zx_status_t Imx227Device::CameraSensor2GetAnalogGain(float* out_gain) {
  std::lock_guard guard(lock_);

  auto status = ReadGainConstants();
  if (status != ZX_OK) {
    return status;
  }

  auto result = Read16(kAnalogGainCodeGlobalReg);
  if (result.is_error()) {
    return result.error();
  }

  *out_gain = AnalogRegValueToTotalGain(result.value());
  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2SetAnalogGain(float gain, float* out_gain) {
  std::lock_guard guard(lock_);

  auto status = ReadGainConstants();
  if (status != ZX_OK) {
    return status;
  }

  auto new_analog_gain = AnalogTotalGainToRegValue(gain);
  if (new_analog_gain != analog_gain_.gain_code_global_) {
    analog_gain_.gain_code_global_ = new_analog_gain;
    analog_gain_.update_gain_ = true;
  }
  *out_gain = AnalogRegValueToTotalGain(analog_gain_.gain_code_global_);

  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2GetDigitalGain(float* out_gain) {
  std::lock_guard guard(lock_);

  auto status = ReadGainConstants();
  if (status) {
    return status;
  }

  auto result = Read16(kDigitalGainGlobalReg);
  if (result.is_error()) {
    return result.error();
  }

  *out_gain = DigitalRegValueToTotalGain(result.value());
  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2SetDigitalGain(float gain, float* out_gain) {
  std::lock_guard guard(lock_);

  auto status = ReadGainConstants();
  if (status) {
    return status;
  }

  auto new_digital_gain = DigitalTotalGainToRegValue(gain);
  if (new_digital_gain != digital_gain_.gain_) {
    digital_gain_.gain_ = new_digital_gain;
    digital_gain_.update_gain_ = true;
  }
  *out_gain = DigitalRegValueToTotalGain(digital_gain_.gain_);

  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2GetIntegrationTime(float* out_int_time) {
  std::lock_guard guard(lock_);

  auto result_cit = Read16(kCoarseIntegrationTimeReg);
  auto result_lps = GetLinesPerSecond();
  if (result_cit.is_error() || result_lps.is_error()) {
    return ZX_ERR_INTERNAL;
  }
  *out_int_time = static_cast<float>(result_cit.value()) / result_lps.value();

  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2SetIntegrationTime(float int_time, float* out_int_time) {
  std::lock_guard guard(lock_);

  auto result = GetLinesPerSecond();
  if (result.is_error()) {
    return result.error();
  }
  uint16_t new_coarse_integration_time = int_time * result.value();

  if (new_coarse_integration_time != integration_time_.coarse_integration_time_) {
    integration_time_.coarse_integration_time_ = new_coarse_integration_time;
    integration_time_.update_integration_time_ = true;
  }
  *out_int_time = int_time;

  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2Update() {
  std::lock_guard guard(lock_);

  auto status = SetGroupedParameterHold(true);

  if (analog_gain_.update_gain_) {
    status = Write16(kAnalogGainCodeGlobalReg, analog_gain_.gain_code_global_);
    if (status != ZX_OK) {
      return status;
    }
    analog_gain_.update_gain_ = false;
  }

  if (digital_gain_.update_gain_) {
    status = Write16(kDigitalGainGlobalReg, digital_gain_.gain_);
    if (status) {
      return status;
    }
    digital_gain_.update_gain_ = false;
  }

  if (integration_time_.update_integration_time_) {
    status = Write16(kCoarseIntegrationTimeReg, integration_time_.coarse_integration_time_);
    if (status) {
      return status;
    }
    integration_time_.update_integration_time_ = false;
  }

  status = SetGroupedParameterHold(false);
  return status;
}

zx_status_t Imx227Device::CameraSensor2GetOtpSize(uint32_t* out_size) {
  *out_size = OTP_TOTAL_SIZE;
  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2GetOtpData(uint32_t byte_count, uint32_t offset,
                                                  zx::vmo* out_otp_data) {
  if ((byte_count + offset) > OTP_TOTAL_SIZE) {
    return ZX_ERR_OUT_OF_RANGE;
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
  *out_otp_data = std::move(vmo);
  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2GetTestPatternMode(uint16_t* out_value) {
  std::lock_guard guard(lock_);

  auto result = Read16(kTestPatternReg);
  if (result.is_error()) {
    zxlogf(ERROR, "%s; Reading the mode failed.", __func__);
    return result.error();
  }
  *out_value = result.value();
  return ZX_OK;
}

zx_status_t Imx227Device::CameraSensor2SetTestPatternMode(uint16_t mode) {
  std::lock_guard guard(lock_);

  if (mode > kNumTestPatternModes) {
    zxlogf(ERROR, "%s; Invalid mode entered.", __func__);
    return ZX_ERR_INVALID_ARGS;
  }

  zx_status_t status = Write16(kTestPatternReg, mode);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s; Writing the mode failed.", __func__);
  }
  return status;
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
      auto result = GetLinesPerSecond();
      if (result.is_error()) {
        return result.error();
      }
      out_value->uint_value = result.value();
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
