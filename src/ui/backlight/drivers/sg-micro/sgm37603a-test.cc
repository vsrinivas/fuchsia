// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sgm37603a.h"

#include <lib/mock-i2c/mock-i2c.h>

#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace backlight {

class MockGpio : public ddk::GpioProtocol<MockGpio> {
 public:
  MockGpio() : proto_{&gpio_protocol_ops_, this} {}

  const gpio_protocol_t* proto() const { return &proto_; }
  const fbl::Vector<uint8_t>& calls() const { return calls_; }

  zx_status_t GpioConfigIn(uint32_t flags) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t GpioConfigOut(uint8_t initial) {
    calls_.push_back(initial);
    return ZX_OK;
  }

  zx_status_t GpioSetAltFunction(uint64_t function) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t GpioRead(uint8_t* out_value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t GpioWrite(uint8_t value) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t GpioReleaseInterrupt() { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t GpioSetPolarity(gpio_polarity_t polarity) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t GpioSetDriveStrength(uint64_t ds_ua, uint64_t* out_actual_ds_ua) {
    return ZX_ERR_NOT_SUPPORTED;
  }

 private:
  gpio_protocol_t proto_;
  fbl::Vector<uint8_t> calls_;
};

class MockSgm37603a : public Sgm37603a {
 public:
  MockSgm37603a(ddk::I2cChannel i2c)
      : Sgm37603a(nullptr, std::move(i2c), ddk::GpioProtocolClient()) {}

  void VerifyGetBrightness(bool power, double brightness) {
    bool pwr;
    double brt;
    EXPECT_OK(GetBacklightState(&pwr, &brt));
    EXPECT_EQ(pwr, power);
    EXPECT_EQ(brt, brightness);
  }

  void VerifySetBrightness(bool power, double brightness) {
    EXPECT_OK(SetBacklightState(power, brightness));
  }

  zx_status_t EnableBacklight() override {
    enable_called_ = true;
    return ZX_OK;
  }

  zx_status_t DisableBacklight() override {
    disable_called_ = true;
    return ZX_OK;
  }

  void Reset() {
    enable_called_ = false;
    disable_called_ = false;
  }

  bool enable_called() const { return enable_called_; }
  bool disable_called() const { return disable_called_; }

 private:
  bool enable_called_ = false;
  bool disable_called_ = false;
};

TEST(BacklightTest, Enable) {
  mock_i2c::MockI2c mock_i2c;
  for (size_t i = 0; i < countof(kDefaultRegValues); i++) {
    mock_i2c.ExpectWriteStop({kDefaultRegValues[i][0], kDefaultRegValues[i][1]});
  }

  MockGpio mock_gpio;

  Sgm37603a test(nullptr, ddk::I2cChannel(mock_i2c.GetProto()),
                 ddk::GpioProtocolClient(mock_gpio.proto()));
  EXPECT_OK(test.EnableBacklight());

  ASSERT_NO_FATAL_FAILURES(mock_i2c.VerifyAndClear());

  ASSERT_EQ(1, mock_gpio.calls().size());
  EXPECT_EQ(1, mock_gpio.calls()[0]);
}

TEST(BacklightTest, Disable) {
  mock_i2c::MockI2c mock_i2c;
  MockGpio mock_gpio;

  Sgm37603a test(nullptr, ddk::I2cChannel(mock_i2c.GetProto()),
                 ddk::GpioProtocolClient(mock_gpio.proto()));
  EXPECT_OK(test.DisableBacklight());
  ASSERT_EQ(1, mock_gpio.calls().size());
  EXPECT_EQ(0, mock_gpio.calls()[0]);
}

TEST(BacklightTest, Brightness) {
  mock_i2c::MockI2c mock_i2c;
  MockSgm37603a test(ddk::I2cChannel(mock_i2c.GetProto()));

  test.VerifySetBrightness(false, 0.5);
  EXPECT_TRUE(test.disable_called());

  test.Reset();
  ASSERT_NO_FATAL_FAILURES(mock_i2c.VerifyAndClear());

  test.VerifyGetBrightness(false, 0);

  double brightness = 0.5;
  uint16_t brightness_value = static_cast<uint16_t>(brightness * kMaxBrightnessRegValue);
  mock_i2c.ExpectWriteStop(
      {kBrightnessLsb, static_cast<uint8_t>(brightness_value & kBrightnessLsbMask)});
  mock_i2c.ExpectWriteStop(
      {kBrightnessMsb, static_cast<uint8_t>(brightness_value >> kBrightnessLsbBits)});

  test.VerifySetBrightness(true, brightness);
  EXPECT_TRUE(test.enable_called());

  test.Reset();
  ASSERT_NO_FATAL_FAILURES(mock_i2c.VerifyAndClear());

  test.VerifyGetBrightness(true, brightness);

  mock_i2c.ExpectWriteStop({kBrightnessLsb, 0}).ExpectWriteStop({kBrightnessMsb, 0});

  test.VerifySetBrightness(true, 0);
  EXPECT_FALSE(test.enable_called());

  test.Reset();
  ASSERT_NO_FATAL_FAILURES(mock_i2c.VerifyAndClear());

  test.VerifyGetBrightness(true, 0);
}

}  // namespace backlight
