// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lp50xx-light.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace lp50xx_light {

class Lp50xxLightTest : public Lp50xxLight {
 public:
  Lp50xxLightTest(zx_device_t* parent)
      : Lp50xxLight(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  virtual zx_status_t InitHelper() {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    if (endpoints.is_error()) {
      return endpoints.error_value();
    }

    fidl::BindServer(loop_.dispatcher(), std::move(endpoints->server), &mock_i2c);

    i2c_ = std::move(endpoints->client);

    mock_i2c.ExpectWriteStop({0x00, 0x40}).ExpectWriteStop({0x01, 0x3C});

    pid_ = PDEV_PID_TI_LP5018;
    led_count_ = 6;

    return loop_.StartThread();
  }

  void set_is_visalia(bool is_visalia) { is_visalia_ = is_visalia; }

  zx_status_t GetRgb(uint32_t index, fuchsia_hardware_light::wire::Rgb* rgb) {
    return Lp50xxLight::GetRgbValue(index, rgb);
  }

  zx_status_t SetRgb(uint32_t index, fuchsia_hardware_light::wire::Rgb rgb) {
    return Lp50xxLight::SetRgbValue(index, rgb);
  }

  zx_status_t SetBrightness(uint32_t index, double brightness) {
    return Lp50xxLight::SetBrightness(index, brightness);
  }

  zx_status_t GetBrightness(uint32_t index, double* brightness) {
    return Lp50xxLight::GetBrightness(index, brightness);
  }

  void Verify() { mock_i2c.VerifyAndClear(); }

  mock_i2c::MockI2c mock_i2c;

 private:
  async::Loop loop_;
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

TEST(Lp50xxLightTest, GetRgbTestVisalia) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  Lp50xxLightTest dut(fake_parent.get());
  EXPECT_OK(dut.Init());
  dut.set_is_visalia(true);

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

  dut.mock_i2c.ExpectWrite({0x13})
      .ExpectReadStop({0xAA})
      .ExpectWrite({0x12})
      .ExpectReadStop({0xBB})
      .ExpectWrite({0x14})
      .ExpectReadStop({0xCC});

  EXPECT_OK(dut.GetRgb(1, &rgb));
  EXPECT_EQ(rgb.red, static_cast<float>((0xAA * 1.0) / (UINT8_MAX * 1.0)));
  EXPECT_EQ(rgb.green, static_cast<float>((0xBB * 1.0) / (UINT8_MAX * 1.0)));
  EXPECT_EQ(rgb.blue, static_cast<float>((0xCC * 1.0) / (UINT8_MAX * 1.0)));

  dut.Verify();
}

TEST(Lp50xxLightTest, SetRgbTestVisalia) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  Lp50xxLightTest dut(fake_parent.get());
  EXPECT_OK(dut.Init());
  dut.set_is_visalia(true);

  fuchsia_hardware_light::wire::Rgb rgb = {};
  rgb.red = static_cast<float>((0xAA * 1.0) / (UINT8_MAX * 1.0));
  rgb.green = static_cast<float>((0xBB * 1.0) / (UINT8_MAX * 1.0));
  rgb.blue = static_cast<float>((0xCC * 1.0) / (UINT8_MAX * 1.0));

  dut.mock_i2c.ExpectWriteStop({0x10, 0xAA})
      .ExpectWriteStop({0x11, 0xBB})
      .ExpectWriteStop({0x0f, 0xCC});

  EXPECT_OK(dut.SetRgb(0, rgb));

  dut.mock_i2c.ExpectWriteStop({0x13, 0xAA})
      .ExpectWriteStop({0x12, 0xBB})
      .ExpectWriteStop({0x14, 0xCC});

  EXPECT_OK(dut.SetRgb(1, rgb));

  dut.Verify();
}

TEST(Lp50xxLightTest, SetBrightnessTest) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  Lp50xxLightTest dut(fake_parent.get());
  EXPECT_OK(dut.Init());

  dut.mock_i2c
      .ExpectWriteStop({0x0a, 128})  // Rounded up
      .ExpectWriteStop({0x0c, 191})  // Rounded down
      .ExpectWriteStop({0x07, 0})
      .ExpectWriteStop({0x09, 255});

  EXPECT_OK(dut.SetBrightness(3, 0.5));
  EXPECT_OK(dut.SetBrightness(5, 0.75));
  EXPECT_OK(dut.SetBrightness(0, 0));
  EXPECT_OK(dut.SetBrightness(2, 1.0));

  dut.Verify();
}

TEST(Lp50xxLightTest, GetBrightnessTest) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
  Lp50xxLightTest dut(fake_parent.get());
  EXPECT_OK(dut.Init());

  dut.mock_i2c.ExpectWrite({0x0a})
      .ExpectReadStop({128})
      .ExpectWrite({0x0c})
      .ExpectReadStop({191})
      .ExpectWrite({0x07})
      .ExpectReadStop({0})
      .ExpectWrite({0x09})
      .ExpectReadStop({255});

  double brightness;
  EXPECT_OK(dut.GetBrightness(3, &brightness));
  EXPECT_EQ(static_cast<uint32_t>(std::round(brightness * 1000)), 502);
  EXPECT_OK(dut.GetBrightness(5, &brightness));
  EXPECT_EQ(static_cast<uint32_t>(std::round(brightness * 1000)), 749);
  EXPECT_OK(dut.GetBrightness(0, &brightness));
  EXPECT_EQ(static_cast<uint32_t>(std::round(brightness * 1000)), 0);
  EXPECT_OK(dut.GetBrightness(2, &brightness));
  EXPECT_EQ(static_cast<uint32_t>(std::round(brightness * 1000)), 1000);

  dut.Verify();
}

}  // namespace lp50xx_light
