// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BACKLIGHT_DRIVERS_SG_MICRO_SGM37603A_H_
#define SRC_UI_BACKLIGHT_DRIVERS_SG_MICRO_SGM37603A_H_

#include <fidl/fuchsia.hardware.backlight/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo.h>
#include <lib/device-protocol/i2c-channel.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>

namespace backlight {

constexpr uint8_t kEnable = 0x10;
constexpr uint8_t kEnableDevice = 0x01;
constexpr uint8_t kEnableLed1 = 0x02;

constexpr uint8_t kBrightnessControl = 0x11;
constexpr uint8_t kBrightnessControlRegisterOnly = 0x00;
constexpr uint8_t kBrightnessControlRampDisabled = 0x00;

constexpr uint8_t kBrightnessLsb = 0x1a;
constexpr uint8_t kBrightnessMsb = 0x19;

constexpr uint8_t kDefaultRegValues[][2] = {
    {kEnable, kEnableDevice | kEnableLed1},
    {kBrightnessControl, kBrightnessControlRegisterOnly | kBrightnessControlRampDisabled},
    {kBrightnessLsb, 0},
    {kBrightnessMsb, 0},
};

constexpr uint16_t kMaxBrightnessRegValue = 0xFFF;
constexpr uint16_t kBrightnessLsbBits = 4;
constexpr uint16_t kBrightnessLsbMask = (0x1 << kBrightnessLsbBits) - 1;

namespace FidlBacklight = fuchsia_hardware_backlight;
class Sgm37603a;
using DeviceType = ddk::Device<Sgm37603a, ddk::Messageable<FidlBacklight::Device>::Mixin>;

class Sgm37603a : public DeviceType, public ddk::EmptyProtocol<ZX_PROTOCOL_BACKLIGHT> {
 public:
  virtual ~Sgm37603a() = default;

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkRelease() { delete this; }

  // Visible for testing.
  Sgm37603a(zx_device_t* parent, ddk::I2cChannel i2c, ddk::GpioProtocolClient reset_gpio)
      : DeviceType(parent), i2c_(i2c), reset_gpio_(reset_gpio) {}

  virtual zx_status_t EnableBacklight();
  virtual zx_status_t DisableBacklight();

  zx_status_t GetBacklightState(bool* power, double* brightness);
  zx_status_t SetBacklightState(bool power, double brightness);

  // FIDL calls
  void GetStateNormalized(GetStateNormalizedRequestView request,
                          GetStateNormalizedCompleter::Sync& completer) override;
  void SetStateNormalized(SetStateNormalizedRequestView request,
                          SetStateNormalizedCompleter::Sync& completer) override;
  void GetStateAbsolute(GetStateAbsoluteRequestView request,
                        GetStateAbsoluteCompleter::Sync& completer) override;
  void SetStateAbsolute(SetStateAbsoluteRequestView request,
                        SetStateAbsoluteCompleter::Sync& completer) override;
  void GetMaxAbsoluteBrightness(GetMaxAbsoluteBrightnessRequestView request,
                                GetMaxAbsoluteBrightnessCompleter::Sync& completer) override;
  void SetNormalizedBrightnessScale(
      SetNormalizedBrightnessScaleRequestView request,
      SetNormalizedBrightnessScaleCompleter::Sync& completer) override;
  void GetNormalizedBrightnessScale(
      GetNormalizedBrightnessScaleRequestView request,
      GetNormalizedBrightnessScaleCompleter::Sync& completer) override;

 private:
  ddk::I2cChannel i2c_;
  ddk::GpioProtocolClient reset_gpio_;
  bool enabled_ = false;
  // TODO(rashaeqbal): Once we also support brightness in nits, consider renaming this to accurately
  // reflect normalized units.
  double brightness_ = 0.0;
};

}  // namespace backlight

#endif  // SRC_UI_BACKLIGHT_DRIVERS_SG_MICRO_SGM37603A_H_
