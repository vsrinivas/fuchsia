// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_BACKLIGHT_TI_LP8556_TI_LP8556_H_
#define ZIRCON_SYSTEM_DEV_BACKLIGHT_TI_LP8556_TI_LP8556_H_

#include <fuchsia/hardware/backlight/llcpp/fidl.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <hw/reg.h>
#include <hwreg/bitfields.h>

namespace ti {

#define LOG_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_SPEW(fmt, ...) zxlogf(SPEW, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define LOG_TRACE zxlogf(INFO, "[%s %d]\n", __func__, __LINE__)

constexpr uint8_t kBacklightControlReg = 0x0;
constexpr uint8_t kDeviceControlReg = 0x1;
constexpr uint8_t kCfg2Reg = 0xA2;

constexpr uint8_t kBacklightOn = 0x85;
constexpr uint8_t kBacklightOff = 0x84;
constexpr uint8_t kCfg2Default = 0x30;

constexpr uint8_t kMaxBrightnessRegValue = 0xFF;

constexpr uint32_t kAOBrightnessStickyReg = (0x04e << 2);
constexpr uint16_t kAOBrightnessStickyBits = 12;
constexpr uint16_t kAOBrightnessStickyMask = ((0x1 << kAOBrightnessStickyBits) - 1);
constexpr uint16_t kAOBrightnessStickyMaxValue = kAOBrightnessStickyMask;

class Lp8556Device;
using DeviceType = ddk::Device<Lp8556Device, ddk::Unbindable, ddk::Messageable>;
namespace FidlBacklight = llcpp::fuchsia::hardware::backlight;

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
                     public FidlBacklight::Device::Interface {
 public:
  Lp8556Device(zx_device_t* parent, ddk::I2cChannel i2c, ddk::MmioBuffer mmio)
      : DeviceType(parent), i2c_(std::move(i2c)), mmio_(std::move(mmio)) {
    auto persistent_brightness = BrightnessStickyReg::Get().ReadFrom(&mmio_);

    if (persistent_brightness.is_valid()) {
      double brightness =
          static_cast<double>(persistent_brightness.brightness()) / kAOBrightnessStickyMaxValue;

      if (SetBacklightState(brightness > 0, brightness) != ZX_OK) {
        LOG_ERROR("Could not set sticky brightness value: %f\n", brightness);
      }
    }

    if ((i2c_.ReadSync(kCfg2Reg, &cfg2_, 1) != ZX_OK) || (cfg2_ == 0)) {
      cfg2_ = kCfg2Default;
    }
  }

  // Methods requried by the ddk mixins
  void DdkUnbind();
  void DdkRelease();
  zx_status_t DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn);

  zx_status_t GetBacklightState(bool* power, double* brightness);
  zx_status_t SetBacklightState(bool power, double brightness);

  double GetDeviceBrightness() { return brightness_; }
  bool GetDevicePower() { return power_; }
  uint8_t GetCfg2() { return cfg2_; }

  // FIDL calls
  void GetStateNormalized(GetStateNormalizedCompleter::Sync _completer) override;
  void SetStateNormalized(FidlBacklight::State state,
                          SetStateNormalizedCompleter::Sync _completer) override;
  void GetStateAbsolute(GetStateAbsoluteCompleter::Sync _completer) override;
  void SetStateAbsolute(FidlBacklight::State state,
                        SetStateAbsoluteCompleter::Sync _completer) override;

 private:
  // TODO(rashaeqbal): Switch from I2C to PWM in order to support a larger brightness range.
  // Needs a PWM driver.
  ddk::I2cChannel i2c_;
  ddk::MmioBuffer mmio_;

  // brightness is set to maximum from bootloader if the persistent brightness sticky register is
  // not set.
  // TODO(rashaeqbal): Once we also support brightness in nits, consider renaming this to accurately
  // reflect normalized units.
  double brightness_ = 1.0;
  bool power_ = true;
  uint8_t cfg2_;
};

}  // namespace ti

#endif  // ZIRCON_SYSTEM_DEV_BACKLIGHT_TI_LP8556_TI_LP8556_H_
