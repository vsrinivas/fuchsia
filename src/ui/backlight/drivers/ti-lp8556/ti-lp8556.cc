// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "ti-lp8556.h"

#include <endian.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/metadata.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/device-protocol/pdev.h>
#include <math.h>

#include <algorithm>

#include <ddktl/fidl.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <pretty/hexdump.h>

#include "src/ui/backlight/drivers/ti-lp8556/ti-lp8556-bind.h"
#include "ti-lp8556Metadata.h"

namespace ti {

enum {
  // These values are shared with the Nelson bootloader, and must be kept in sync.
  kPanelTypeUnknown = 0,
  kPanelTypeKdFiti9364 = 1,
  kPanelTypeBoeFiti9364 = 2,
  kPanelTypeInxFiti9364 = 3,
  kPanelTypeKdFiti9365 = 4,
  kPanelTypeBoeFiti9365 = 5,
  kPanelTypeBoeSit7703 = 6,
};

// Refer to <internal>/vendor/amlogic/video-common/ambient_temp/lp8556.cc
// Lookup tables containing the slope and y-intercept for a linear equation used
// to fit the (power / |brightness_to_current_scalar_|) per vendor for
// brightness levels below |kMinTableBrightness|. The power can be calculated
// from these scalars by:
// (slope * brightness + intercept) * |brightness_to_current_scalar_|.
constexpr std::array<double,
                     static_cast<std::size_t>(Lp8556Device::PanelType::kNumTypes)>
    kLowBrightnessSlopeTable = {
        22.4,  // PanelType::kBoe
        22.1,  // PanelType::kInx
        22.2,  // PanelType::kKd
        22.2,  // PanelType::kUnknown
};
constexpr std::array<double,
                     static_cast<std::size_t>(Lp8556Device::PanelType::kNumTypes)>
    kLowBrightnessInterceptTable = {
        1236.0,  // PanelType::kBoe
        1431.0,  // PanelType::kInx
        1319.0,  // PanelType::kKd
        1329.0,  // PanelType::kUnknown
};

// Lookup tables for backlight driver voltage as a function of the backlight
// brightness. The index for each sub-table corresponds to a PanelType, and
// allows for the backlight voltage to vary with panel vendor. Starting from a
// brightness level of |kMinTableBrightness|, each index of each sub-table
// corresponds to a jump of |kBrightnessStep| in brightness up to the maximum
// value of |kMaxBrightnessSetting|.
constexpr std::array<std::array<double, kTableSize>,
                     static_cast<std::size_t>(Lp8556Device::PanelType::kNumTypes)>
    kVoltageTable = {{
        // PanelType::kBoe
        {19.80, 19.80, 19.80, 19.80, 19.90, 20.00, 20.10, 20.20, 20.30, 20.40, 20.50, 20.53, 20.53,
         20.53, 20.53, 20.53},
        // PanelType::kInx
        {19.70, 19.70, 19.70, 19.70, 19.80, 19.90, 20.00, 20.10, 20.20, 20.27, 20.30, 20.30, 20.30,
         20.30, 20.30, 20.30},
        // PanelType::kKd
        {19.67, 19.67, 19.67, 19.67, 19.77, 19.93, 20.03, 20.13, 20.20, 20.27, 20.37, 20.37, 20.37,
         20.37, 20.37, 20.37},
        // PanelType:kUnknown
        {19.72, 19.72, 19.72, 19.72, 19.82, 19.94, 20.04, 20.14, 20.23, 20.31, 20.39, 20.40, 20.40,
         20.40, 20.40, 20.40},
    }};

// Lookup table for backlight driver efficiency as a function of the backlight
// brightness. Starting from a brightness level of |kMinTableBrightness|, each
// index of the table corresponds to a jump of |kBrightnessStep| in brightness
// up to the maximum value of |kMaxBrightnessSetting|.
constexpr std::array<double, kTableSize> kEfficiencyTable = {
    0.6680, 0.7784, 0.8240, 0.8484, 0.8634, 0.8723, 0.8807, 0.8860,
    0.8889, 0.8915, 0.8953, 0.8983, 0.9003, 0.9034, 0.9049, 0.9060};

// The max current value in the table is determined by the value of the three
// max current bits within the Lp8556 CFG1 register. The value of these bits can
// be obtained from the max_current sysfs node exposed by the driver. The
// current values in the table are expressed in mA.
constexpr std::array<double, 8> kMaxCurrentTable = {5.0, 10.0, 15.0, 20.0, 23.0, 25.0, 30.0, 50.0};

void Lp8556Device::DdkRelease() { delete this; }

zx_status_t Lp8556Device::GetBacklightState(bool* power, double* brightness) {
  *power = power_;
  *brightness = brightness_;
  return ZX_OK;
}

zx_status_t Lp8556Device::SetBacklightState(bool power, double brightness) {
  brightness = std::max(brightness, 0.0);
  brightness = std::min(brightness, 1.0);
  uint16_t brightness_reg_value = static_cast<uint16_t>(ceil(brightness * kBrightnessRegMaxValue));
  if (brightness != brightness_) {
    // LSB should be updated before MSB. Writing to MSB triggers the brightness change.
    uint8_t buf[2];
    buf[0] = kBacklightBrightnessLsbReg;
    buf[1] = static_cast<uint8_t>(brightness_reg_value & kBrightnessLsbMask);
    zx_status_t status = i2c_.WriteSync(buf, sizeof(buf));
    if (status != ZX_OK) {
      LOG_ERROR("Failed to set brightness LSB register\n");
      return status;
    }

    uint8_t msb_reg_value;
    status = i2c_.ReadSync(kBacklightBrightnessMsbReg, &msb_reg_value, 1);
    if (status != ZX_OK) {
      LOG_ERROR("Failed to get brightness MSB register\n");
      return status;
    }

    // The low 4-bits contain the brightness MSB. Keep the remaining bits unchanged.
    msb_reg_value &= static_cast<uint8_t>(~kBrightnessMsbByteMask);
    msb_reg_value |=
        (static_cast<uint8_t>((brightness_reg_value & kBrightnessMsbMask) >> kBrightnessMsbShift));

    buf[0] = kBacklightBrightnessMsbReg;
    buf[1] = msb_reg_value;
    status = i2c_.WriteSync(buf, sizeof(buf));
    if (status != ZX_OK) {
      LOG_ERROR("Failed to set brightness MSB register\n");
      return status;
    }

    auto persistent_brightness = BrightnessStickyReg::Get().ReadFrom(&mmio_);
    persistent_brightness.set_brightness(brightness_reg_value & kBrightnessRegMask);
    persistent_brightness.set_is_valid(1);
    persistent_brightness.WriteTo(&mmio_);
  }

  if (power != power_) {
    uint8_t buf[2];
    buf[0] = kDeviceControlReg;
    buf[1] = kDeviceControlDefaultValue | (power ? kBacklightOn : 0);
    zx_status_t status = i2c_.WriteSync(buf, sizeof(buf));
    if (status != ZX_OK) {
      LOG_ERROR("Failed to set device control register\n");
      return status;
    }

    if (power) {
      for (size_t i = 0; i < metadata_.register_count; i += 2) {
        if ((status = i2c_.WriteSync(&metadata_.registers[i], 2)) != ZX_OK) {
          LOG_ERROR("Failed to set register 0x%02x: %d\n", metadata_.registers[i], status);
          return status;
        }
      }

      buf[0] = kCfg2Reg;
      buf[1] = cfg2_;
      status = i2c_.WriteSync(buf, sizeof(buf));
      if (status != ZX_OK) {
        LOG_ERROR("Failed to set cfg2 register\n");
        return status;
      }
    }
  }

  // update internal values
  power_ = power;
  brightness_ = brightness;
  power_property_.Set(power_);
  brightness_property_.Set(brightness_);
  backlight_power_ = GetBacklightPower(brightness_reg_value);
  power_watts_property_.Set(backlight_power_);
  return ZX_OK;
}

void Lp8556Device::GetStateNormalized(GetStateNormalizedRequestView request,
                                      GetStateNormalizedCompleter::Sync& completer) {
  FidlBacklight::wire::State state = {};
  auto status = GetBacklightState(&state.backlight_on, &state.brightness);
  if (status == ZX_OK) {
    completer.ReplySuccess(state);
  } else {
    completer.ReplyError(status);
  }
}

void Lp8556Device::SetStateNormalized(SetStateNormalizedRequestView request,
                                      SetStateNormalizedCompleter::Sync& completer) {
  auto status = SetBacklightState(request->state.backlight_on, request->state.brightness);
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void Lp8556Device::GetStateAbsolute(GetStateAbsoluteRequestView request,
                                    GetStateAbsoluteCompleter::Sync& completer) {
  if (!max_absolute_brightness_nits_.has_value()) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }
  if (scale_ != calibrated_scale_) {
    LOG_ERROR("Can't get absolute state with non-calibrated current scale\n");
    completer.ReplyError(ZX_ERR_BAD_STATE);
    return;
  }

  FidlBacklight::wire::State state = {};
  auto status = GetBacklightState(&state.backlight_on, &state.brightness);
  if (status == ZX_OK) {
    state.brightness *= max_absolute_brightness_nits_.value();
    completer.ReplySuccess(state);
  } else {
    completer.ReplyError(status);
  }
}

void Lp8556Device::SetStateAbsolute(SetStateAbsoluteRequestView request,
                                    SetStateAbsoluteCompleter::Sync& completer) {
  if (!max_absolute_brightness_nits_.has_value()) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  // Restore the calibrated current scale that the bootloader set. This and the maximum brightness
  // are the only values we have that can be used to set the absolute brightness in nits.
  auto status = SetCurrentScale(calibrated_scale_);
  if (status != ZX_OK) {
    completer.ReplyError(status);
    return;
  }

  status = SetBacklightState(request->state.backlight_on,
                             request->state.brightness / max_absolute_brightness_nits_.value());
  if (status == ZX_OK) {
    completer.ReplySuccess();
  } else {
    completer.ReplyError(status);
  }
}

void Lp8556Device::GetMaxAbsoluteBrightness(GetMaxAbsoluteBrightnessRequestView request,
                                            GetMaxAbsoluteBrightnessCompleter::Sync& completer) {
  if (max_absolute_brightness_nits_.has_value()) {
    completer.ReplySuccess(max_absolute_brightness_nits_.value());
  } else {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
}

void Lp8556Device::SetNormalizedBrightnessScale(
    SetNormalizedBrightnessScaleRequestView request,
    SetNormalizedBrightnessScaleCompleter::Sync& completer) {
  if (!metadata_.allow_set_current_scale) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
    return;
  }

  double scale = std::clamp(request->scale, 0.0, 1.0);

  zx_status_t status = SetCurrentScale(static_cast<uint16_t>(scale * kBrightnessRegMaxValue));
  if (status != ZX_OK) {
    completer.ReplyError(status);
  } else {
    completer.ReplySuccess();
  }
}

void Lp8556Device::GetNormalizedBrightnessScale(
    GetNormalizedBrightnessScaleRequestView request,
    GetNormalizedBrightnessScaleCompleter::Sync& completer) {
  if (!metadata_.allow_set_current_scale) {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  } else {
    completer.ReplySuccess(static_cast<double>(scale_) / kBrightnessRegMaxValue);
  }
}

void Lp8556Device::GetPowerWatts(GetPowerWattsRequestView request,
                                 GetPowerWattsCompleter::Sync& completer) {
  // Only supported on Nelson for now.
  if (board_pid_ == PDEV_PID_NELSON) {
    completer.ReplySuccess(backlight_power_);
  } else {
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
}

void Lp8556Device::GetVoltageVolts(GetVoltageVoltsRequestView request,
                                   GetVoltageVoltsCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void Lp8556Device::DdkMessage(fidl::IncomingMessage&& msg, DdkTransaction& txn) {
  if (fidl::WireTryDispatch<FidlBacklight::Device>(this, msg, &txn) ==
      ::fidl::DispatchResult::kFound) {
    return;
  }
  fidl::WireDispatch<FidlPowerSensor::Device>(this, std::move(msg), &txn);
}

zx_status_t Lp8556Device::Init() {
  root_ = inspector_.GetRoot().CreateChild("ti-lp8556");
  double brightness_nits = 0.0;
  size_t actual;
  zx_status_t status =
      device_get_fragment_metadata(parent(), "pdev", DEVICE_METADATA_BACKLIGHT_MAX_BRIGHTNESS_NITS,
                                   &brightness_nits, sizeof(brightness_nits), &actual);
  if (status == ZX_OK && actual == sizeof(brightness_nits)) {
    SetMaxAbsoluteBrightnessNits(brightness_nits);
  }
  status = device_get_fragment_metadata(parent(), "pdev", DEVICE_METADATA_PRIVATE, &metadata_,
                                        sizeof(metadata_), &actual);
  // Supplying this metadata is optional.
  if (status == ZX_OK) {
    if (metadata_.register_count % (2 * sizeof(uint8_t)) != 0) {
      LOG_ERROR("Register metadata is invalid. Register count (%u) is not a multiple of %zu\n",
                metadata_.register_count, 2 * sizeof(uint8_t));
      return ZX_ERR_INVALID_ARGS;
    } else if (actual != sizeof(metadata_)) {
      LOG_ERROR(
          "Too many registers specified in metadata. Expected size %zu, got %zu. Got metadata with "
          "value\n",
          sizeof(metadata_), actual);
      char output_buffer[80];
      for (size_t count = 0; count < actual; count += 16) {
        FILE* f = fmemopen(output_buffer, sizeof(output_buffer), "w");
        if (!f) {
          zxlogf(ERROR, "Couldn't open buffer. Returning.");
          return status;
        }
        hexdump_very_ex(reinterpret_cast<uint8_t*>(&metadata_) + count,
                        std::min(actual - count, 16UL), count, hexdump_stdio_printf, f);
        fclose(f);
        zxlogf(ERROR, "%s", output_buffer);
      }
      return ZX_ERR_OUT_OF_RANGE;
    }

    for (size_t i = 0; i < metadata_.register_count; i += 2) {
      if ((status = i2c_.WriteSync(&metadata_.registers[i], 2)) != ZX_OK) {
        LOG_ERROR("Failed to set register 0x%02x: %d\n", metadata_.registers[i], status);
        return status;
      }
    }
  }

  status = device_get_fragment_metadata(parent(), "pdev", DEVICE_METADATA_BOARD_PRIVATE,
                                        &panel_type_id_, sizeof(panel_type_id_), &actual);
  if (status != ZX_OK) {
    panel_type_id_ = kPanelTypeUnknown;
  } else if (actual != sizeof(panel_type_id_)) {
    LOG_ERROR("Unexpected panel ID size: %zu", actual);
    return ZX_ERR_BAD_STATE;
  }

  ddk::PDevProtocolClient pdev(parent(), "pdev");
  if (pdev.is_valid()) {
    pdev_board_info_t board_info{};
    if ((status = pdev.GetBoardInfo(&board_info)) == ZX_OK) {
      board_pid_ = board_info.pid;
    }
  }

  auto persistent_brightness = BrightnessStickyReg::Get().ReadFrom(&mmio_);
  if (persistent_brightness.is_valid()) {
    persistent_brightness_property_ =
        root_.CreateUint("persistent_brightness", persistent_brightness.brightness());
  }

  if ((status = ReadInitialState()) != ZX_OK) {
    return status;
  }

  brightness_property_ = root_.CreateDouble("brightness", brightness_);
  scale_property_ = root_.CreateUint("scale", scale_);
  calibrated_scale_property_ = root_.CreateUint("calibrated_scale", calibrated_scale_);
  power_property_ = root_.CreateBool("power", power_);
  power_watts_property_ = root_.CreateDouble("power_watts", backlight_power_);

  board_pid_property_ = root_.CreateUint("board_pid", board_pid_);
  panel_id_property_ = root_.CreateUint("panel_id", panel_type_id_);
  panel_type_property_ = root_.CreateUint("panel_type", static_cast<uint32_t>(GetPanelType()));

  return ZX_OK;
}

zx_status_t Lp8556Device::SetCurrentScale(uint16_t scale) {
  scale &= kBrightnessRegMask;

  if (scale == scale_) {
    return ZX_OK;
  }

  uint8_t msb_reg_value;
  zx_status_t status = i2c_.ReadSync(kCfgReg, &msb_reg_value, sizeof(msb_reg_value));
  if (status != ZX_OK) {
    LOG_ERROR("Failed to get current scale register: %d", status);
    return status;
  }
  msb_reg_value &= ~kBrightnessMsbByteMask;

  const uint8_t buf[] = {
      kCurrentLsbReg,
      static_cast<uint8_t>(scale & kBrightnessLsbMask),
      static_cast<uint8_t>(msb_reg_value | (scale >> kBrightnessMsbShift)),
  };
  if ((status = i2c_.WriteSync(buf, sizeof(buf))) != ZX_OK) {
    LOG_ERROR("Failed to set current scale register: %d", status);
    return status;
  }

  scale_ = scale;
  scale_property_.Set(scale);
  return ZX_OK;
}

double Lp8556Device::GetBacklightPower(double backlight_brightness) {
  if (board_pid_ != PDEV_PID_NELSON) {
    return 0;
  }

  // For brightness values less than |kMinTableBrightness|, estimate the power
  // on a per-vendor basis from a linear equation derived from validation data.
  if (backlight_brightness < kMinTableBrightness) {
    std::size_t panel_type_index = static_cast<std::size_t>(GetPanelType());
    double slope = kLowBrightnessSlopeTable[panel_type_index];
    double intercept = kLowBrightnessInterceptTable[panel_type_index];
    return (slope * backlight_brightness + intercept) * GetBrightnesstoCurrentScalar();
  }

  // For brightness values in the range [|kMinTableBrightness|,
  // |kMaxBrightnessSetting|], use the voltage and efficiency lookup tables
  // derived from validation data to estimate the power.
  double backlight_voltage = GetBacklightVoltage(backlight_brightness, GetPanelType());
  double current_amp = GetBrightnesstoCurrentScalar() * backlight_brightness;
  double driver_efficiency = GetDriverEfficiency(backlight_brightness);
  return backlight_voltage * current_amp / driver_efficiency;
}

double Lp8556Device::GetBrightnesstoCurrentScalar() {
  double max_current_amp = max_current_ / kMilliampPerAmp;
  // The setpoint current refers to the backlight current for a single driver
  // channel, assuming that the backlight brightness setting is at its max value
  // of 4095 (100%).
  double setpoint_current_amp = (scale_ / kMaxCurrentSetting) * max_current_amp;
  // The scalar returned is equal to:
  // 6 Driver Channels * Setpoint Current per Channel / Max Brightness Setting
  // When this value is multiplied by the backlight brightness setting, it
  // yields the backlight current in Amps.
  return kNumBacklightDriverChannels * setpoint_current_amp / kMaxBrightnessSetting;
}

double Lp8556Device::GetBacklightVoltage(double backlight_brightness, PanelType panel_type) {
  std::size_t panel_type_index = static_cast<std::size_t>(panel_type);

  // Backlight is at max brightness
  if (backlight_brightness == kMaxBrightnessSetting) {
    return kVoltageTable[panel_type_index].back();
  }

  // Backlight is at |kMinTableBrightness|
  if (backlight_brightness == kMinTableBrightness) {
    return kVoltageTable[panel_type_index].front();
  }

  double integral;
  double fractional = modf(backlight_brightness / kBrightnessStep, &integral);
  std::size_t table_index = static_cast<std::size_t>(integral) - 1;
  if (table_index + 1 >= kVoltageTable[panel_type_index].size()) {
    LOG_ERROR("Invalid backlight brightness: %f", backlight_brightness);
    return kVoltageTable[panel_type_index].back();
  }
  double lower_voltage = kVoltageTable[panel_type_index][table_index];
  double upper_voltage = kVoltageTable[panel_type_index][table_index + 1];
  return (upper_voltage - lower_voltage) * fractional + lower_voltage;
}

double Lp8556Device::GetDriverEfficiency(double backlight_brightness) {
  // Backlight is at max brightness
  if (backlight_brightness == kMaxBrightnessSetting) {
    return kEfficiencyTable.back();
  }
  // Backlight is at |kMinTableBrightness|
  if (backlight_brightness == kMinTableBrightness) {
    return kEfficiencyTable.front();
  }
  double integral;
  double fractional = modf(backlight_brightness / kBrightnessStep, &integral);
  std::size_t table_index = static_cast<std::size_t>(integral) - 1;
  if (table_index + 1 >= kEfficiencyTable.size()) {
    LOG_ERROR("Invalid backlight brightness: %f", backlight_brightness);
    return kEfficiencyTable.back();
  }
  double lower_efficiency = kEfficiencyTable[table_index];
  double upper_efficiency = kEfficiencyTable[table_index + 1];
  return (upper_efficiency - lower_efficiency) * fractional + lower_efficiency;
}

Lp8556Device::PanelType Lp8556Device::GetPanelType() {
  switch (panel_type_id_) {
    case kPanelTypeBoeFiti9364:
    case kPanelTypeBoeFiti9365:
    case kPanelTypeBoeSit7703:
      return Lp8556Device::PanelType::kBoe;
    case kPanelTypeInxFiti9364:
      return Lp8556Device::PanelType::kInx;
    case kPanelTypeKdFiti9364:
    case kPanelTypeKdFiti9365:
      return Lp8556Device::PanelType::kKd;
    case kPanelTypeUnknown:
    default:
      return Lp8556Device::PanelType::kUnknown;
  }
}

zx_status_t Lp8556Device::ReadInitialState() {
  if ((i2c_.ReadSync(kCfg2Reg, &cfg2_, 1) != ZX_OK) || (cfg2_ == 0)) {
    cfg2_ = kCfg2Default;
  }

  uint8_t buf[2];
  zx_status_t status = i2c_.ReadSync(kCurrentLsbReg, buf, sizeof(buf));
  if (status != ZX_OK) {
    LOG_ERROR("Could not read current scale value: %d\n", status);
    return status;
  }
  scale_ = static_cast<uint16_t>(buf[0] | (buf[1] << kBrightnessMsbShift)) & kBrightnessRegMask;
  calibrated_scale_ = scale_;

  if ((status = i2c_.ReadSync(kBacklightBrightnessLsbReg, buf, sizeof(buf))) == ZX_OK) {
    uint16_t brightness_reg;
    memcpy(&brightness_reg, buf, sizeof(brightness_reg));
    brightness_reg = le16toh(brightness_reg) & kBrightnessRegMask;
    brightness_ = static_cast<double>(brightness_reg) / kBrightnessRegMaxValue;
  } else {
    LOG_ERROR("Could not read backlight brightness: %d\n", status);
    brightness_ = 1.0;
    backlight_power_ = 0;
  }

  uint8_t device_control;
  status = i2c_.ReadSync(kDeviceControlReg, &device_control, sizeof(device_control));
  if (status == ZX_OK) {
    power_ = device_control & kBacklightOn;
  } else {
    LOG_ERROR("Could not read backlight power: %d\n", status);
    power_ = true;
  }

  // max_absolute_brightness_nits will be initialized in SetMaxAbsoluteBrightnessNits.
  uint8_t max_current_idx;
  i2c_.ReadSync(kCfgReg, &max_current_idx, sizeof(max_current_idx));
  max_current_idx = (max_current_idx >> 4) & 0b111;
  max_current_ = kMaxCurrentTable[max_current_idx];

  backlight_power_ = GetBacklightPower(brightness_);

  return ZX_OK;
}

zx_status_t ti_lp8556_bind(void* ctx, zx_device_t* parent) {
  // Get platform device protocol
  auto pdev = ddk::PDev::FromFragment(parent);
  if (!pdev.is_valid()) {
    LOG_ERROR("Could not get PDEV protocol\n");
    return ZX_ERR_NO_RESOURCES;
  }

  // Map MMIO
  std::optional<fdf::MmioBuffer> mmio;
  zx_status_t status = pdev.MapMmio(0, &mmio);
  if (status != ZX_OK) {
    LOG_ERROR("Could not map mmio %d\n", status);
    return status;
  }

  // Obtain I2C protocol needed to control backlight
  ddk::I2cChannel i2c(parent, "i2c");
  if (!i2c.is_valid()) {
    LOG_ERROR("Could not obtain I2C protocol\n");
    return ZX_ERR_NO_RESOURCES;
  }

  fbl::AllocChecker ac;
  auto dev =
      fbl::make_unique_checked<ti::Lp8556Device>(&ac, parent, std::move(i2c), *std::move(mmio));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  if ((status = dev->Init()) != ZX_OK) {
    return status;
  }

  status = dev->DdkAdd(ddk::DeviceAddArgs("ti-lp8556").set_inspect_vmo(dev->InspectVmo()));
  if (status != ZX_OK) {
    LOG_ERROR("Could not add device\n");
    return status;
  }

  // devmgr is now in charge of memory for dev
  __UNUSED auto ptr = dev.release();

  return status;
}

static constexpr zx_driver_ops_t ti_lp8556_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = ti_lp8556_bind;
  return ops;
}();

}  // namespace ti

ZIRCON_DRIVER(ti_lp8556, ti::ti_lp8556_driver_ops, "TI-LP8556", "0.1");
