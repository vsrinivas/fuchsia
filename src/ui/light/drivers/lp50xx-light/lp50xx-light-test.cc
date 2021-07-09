// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lp50xx-light.h"

#include <lib/ddk/platform-defs.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace lp50xx_light {

class Lp50xxLightTest : public Lp50xxLight {
 public:
  Lp50xxLightTest(zx_device_t* parent) : Lp50xxLight(parent) {}

  virtual zx_status_t InitHelper() {
    auto proto = ddk::I2cProtocolClient(mock_i2c.GetProto());
    i2c_ = std::move(proto);

    mock_i2c.ExpectWriteStop({0x00, 0x40}).ExpectWriteStop({0x01, 0x3C});

    pid_ = PDEV_PID_TI_LP5018;
    led_count_ = 6;
    return ZX_OK;
  }

  zx_status_t GetRgb(uint32_t index, fuchsia_hardware_light::wire::Rgb* rgb) {
    return Lp50xxLight::GetRgbValue(index, rgb);
  }

  zx_status_t SetRgb(uint32_t index, fuchsia_hardware_light::wire::Rgb rgb) {
    return Lp50xxLight::SetRgbValue(index, rgb);
  }

  void Verify() { mock_i2c.VerifyAndClear(); }

  mock_i2c::MockI2c mock_i2c;
};

TEST(Lp50xxLightTest, InitTest) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  Lp50xxLightTest dut(fake_parent.get());
  EXPECT_OK(dut.Init());
  dut.Verify();
}

TEST(Lp50xxLightTest, GetRgbTest) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  Lp50xxLightTest dut(fake_parent.get());
  EXPECT_OK(dut.Init());

  dut.mock_i2c.ExpectWrite({0x10})
      .ExpectReadStop({0xAA})
      .ExpectWrite({0x11})
      .ExpectReadStop({0xBB})
      .ExpectWrite({0x0f})
      .ExpectReadStop({0xCC});

  fuchsia_hardware_light::wire::Rgb rgb = {};
  EXPECT_OK(dut.GetRgb(0, &rgb));
  EXPECT_EQ(rgb.red, static_cast<float>((0xAA * 1.0) / (UINT8_MAX * 1.0)));
  EXPECT_EQ(rgb.green, static_cast<float>((0xBB * 1.0) / (UINT8_MAX * 1.0)));
  EXPECT_EQ(rgb.blue, static_cast<float>((0xCC * 1.0) / (UINT8_MAX * 1.0)));

  dut.Verify();
}

TEST(Lp50xxLightTest, SetRgbTest) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  Lp50xxLightTest dut(fake_parent.get());
  EXPECT_OK(dut.Init());

  fuchsia_hardware_light::wire::Rgb rgb = {};
  rgb.red = static_cast<float>((0xAA * 1.0) / (UINT8_MAX * 1.0));
  rgb.green = static_cast<float>((0xBB * 1.0) / (UINT8_MAX * 1.0));
  rgb.blue = static_cast<float>((0xCC * 1.0) / (UINT8_MAX * 1.0));

  dut.mock_i2c.ExpectWriteStop({0x10, 0xAA})
      .ExpectWriteStop({0x11, 0xBB})
      .ExpectWriteStop({0x0f, 0xCC});

  EXPECT_OK(dut.SetRgb(0, rgb));

  dut.Verify();
}

}  // namespace lp50xx_light
