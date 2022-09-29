// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BACKLIGHT_DRIVERS_TI_LP8556_TI_LP8556_H_
#define SRC_UI_BACKLIGHT_DRIVERS_TI_LP8556_TI_LP8556_H_

#include <fidl/fuchsia.hardware.backlight/cpp/wire.h>
#include <fidl/fuchsia.hardware.power.sensor/cpp/wire.h>
#include <lib/ddk/hw/reg.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mmio/mmio.h>

#include <optional>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <hwreg/bitfields.h>

#include "ti-lp8556Metadata.h"

namespace ti {

#define LOG_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_SPEW(fmt, ...) zxlogf(TRACE, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_TRACE zxlogf(INFO, "[%s %d]", __func__, __LINE__)

constexpr uint8_t kBacklightBrightnessLsbReg = 0x10;
constexpr uint8_t kBacklightBrightnessMsbReg = 0x11;
constexpr uint8_t kDeviceControlReg = 0x1;
constexpr uint8_t kCurrentLsbReg = 0xA0;
constexpr uint8_t kCfgReg = 0xA1;
constexpr uint8_t kCfg2Reg = 0xA2;
constexpr uint32_t kAOBrightnessStickyReg = (0x04e << 2);

constexpr uint8_t kBacklightOn = 1;
constexpr uint8_t kDeviceControlDefaultValue = 0x84;
constexpr uint8_t kCfg2Default = 0x30;

constexpr uint16_t kBrightnessRegMask = 0xFFF;
constexpr uint16_t kBrightnessRegMaxValue = kBrightnessRegMask;

constexpr uint16_t kBrightnessMsbShift = 8;
constexpr uint16_t kBrightnessLsbMask = 0xFF;
constexpr uint8_t kBrightnessMsbByteMask = 0xF;
constexpr uint16_t kBrightnessMsbMask = (kBrightnessMsbByteMask << kBrightnessMsbShift);

constexpr int kTableSize = 16;
constexpr int kBrightnessStep = 256;
constexpr float kMinTableBrightness = 256;

constexpr float kMaxCurrentSetting = 4095;
constexpr float kMinBrightnessSetting = 0;
constexpr float kMaxBrightnessSetting = 4095;
constexpr int kNumBacklightDriverChannels = 6;

constexpr int kMilliampPerAmp = 1000;

class Lp8556Device;
using DeviceType = ddk::Device<Lp8556Device, ddk::MessageableManual>;
namespace FidlBacklight = fuchsia_hardware_backlight;
namespace FidlPowerSensor = fuchsia_hardware_power_sensor;

class BrightnessStickyReg : public hwreg::RegisterBase<BrightnessStickyReg, uint32_t> {
 public:
  // This bit is used to distinguish between a zero register value and an unset value.
  // A zero value indicates that the sticky register has not been set (so a default of 100%
  // brightness will be used by the bootloader).
  // With this bit set, a zero brightness value is encoded as 0x1000 to distinguish it from an unset
  // value.
  DEF_BIT(12, is_valid);
  DEF_FIELD(11, 0, brightness);

  static auto Get() { return hwreg::RegisterAddr<BrightnessStickyReg>(kAOBrightnessStickyReg); }
};

class Lp8556Device : public DeviceType,
                     public ddk::EmptyProtocol<ZX_PROTOCOL_BACKLIGHT>,
                     public fidl::WireServer<FidlBacklight::Device>,
                     public fidl::WireServer<FidlPowerSensor::Device> {
 public:
  Lp8556Device(zx_device_t* parent, ddk::I2cChannel i2c, fdf::MmioBuffer mmio)
      : DeviceType(parent), i2c_(std::move(i2c)), mmio_(std::move(mmio)) {}

  zx_status_t Init();

  // Methods required by the ddk mixins
  void DdkRelease();
  void DdkMessage(fidl::IncomingHeaderAndMessage&& msg, DdkTransaction& txn);

  zx_status_t GetBacklightState(bool* power, double* brightness);
  zx_status_t SetBacklightState(bool power, double brightness);

  double GetDeviceBrightness() { return brightness_; }
  bool GetDevicePower() { return power_; }
  uint8_t GetCfg2() { return cfg2_; }
  void SetMaxAbsoluteBrightnessNits(double brightness_nits) {
    max_absolute_brightness_nits_ = brightness_nits;
    if (max_absolute_brightness_nits_property_) {
      max_absolute_brightness_nits_property_.Set(brightness_nits);
    } else {
      max_absolute_brightness_nits_property_ =
          root_.CreateDouble("max_absolute_brightness_nits", brightness_nits);
    }
  }

  zx::vmo InspectVmo() { return inspector_.DuplicateVmo(); }
  enum class PanelType {
    kBoe = 0,
    kInx = 1,
    kKd = 2,
    kUnknown = 3,
    kNumTypes = 4,
  };

  double GetBacklightPower(double backlight_brightness);
  double GetBrightnesstoCurrentScalar();
  double GetBacklightVoltage(double backlight_brightness, PanelType panel_type);
  double GetDriverEfficiency(double backlight_brightness);
  PanelType GetPanelType();

  // FIDL calls
  void GetStateNormalized(GetStateNormalizedCompleter::Sync& completer) override;
  void SetStateNormalized(SetStateNormalizedRequestView request,
                          SetStateNormalizedCompleter::Sync& completer) override;
  // Note: the device is calibrated at the factory to find a normalized brightness scale value that
  // corresponds to a set maximum brightness in nits. GetStateAbsolute() will return an error if
  // the normalized brightness scale is not set to the calibrated value, as there is no universal
  // way to map other scale values to absolute brightness.
  void GetStateAbsolute(GetStateAbsoluteCompleter::Sync& completer) override;
  // Note: this changes the normalized brightness scale back to the calibrated value in order to set
  // the absolute brightness.
  void SetStateAbsolute(SetStateAbsoluteRequestView request,
                        SetStateAbsoluteCompleter::Sync& completer) override;
  void GetMaxAbsoluteBrightness(GetMaxAbsoluteBrightnessCompleter::Sync& completer) override;
  void SetNormalizedBrightnessScale(
      SetNormalizedBrightnessScaleRequestView request,
      SetNormalizedBrightnessScaleCompleter::Sync& completer) override;
  void GetNormalizedBrightnessScale(
      GetNormalizedBrightnessScaleCompleter::Sync& completer) override;

  void GetPowerWatts(GetPowerWattsCompleter::Sync& completer) override;

  void GetVoltageVolts(GetVoltageVoltsCompleter::Sync& completer) override;

 private:
  zx_status_t SetCurrentScale(uint16_t scale);
  zx_status_t ReadInitialState();

  inspect::Inspector inspector_;
  inspect::Node root_;

  // TODO(rashaeqbal): Switch from I2C to PWM in order to support a larger brightness range.
  // Needs a PWM driver.
  ddk::I2cChannel i2c_;
  fdf::MmioBuffer mmio_;

  // brightness is set to maximum from bootloader if the persistent brightness sticky register is
  // not set.
  double brightness_ = 1.0;
  uint16_t scale_ = {};
  uint16_t calibrated_scale_ = {};
  bool power_ = true;
  uint8_t cfg2_;
  std::optional<double> max_absolute_brightness_nits_;

  inspect::DoubleProperty brightness_property_;
  inspect::UintProperty persistent_brightness_property_;
  inspect::UintProperty scale_property_;
  inspect::UintProperty calibrated_scale_property_;
  inspect::BoolProperty power_property_;
  inspect::DoubleProperty max_absolute_brightness_nits_property_;
  inspect::DoubleProperty power_watts_property_;
  inspect::UintProperty board_pid_property_;
  inspect::UintProperty panel_id_property_;
  inspect::UintProperty panel_type_property_;
  TiLp8556Metadata metadata_ = {.allow_set_current_scale = false};
  uint32_t panel_type_id_ = 0;
  uint32_t board_pid_ = 0;
  double backlight_power_ = 0;
  double max_current_ = 0.0;
};

}  // namespace ti

#endif  // SRC_UI_BACKLIGHT_DRIVERS_TI_LP8556_TI_LP8556_H_
