// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tcs3400.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/ddk/metadata.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <ddktl/metadata/light-sensor.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace tcs {

struct Tcs3400Test : public zxtest::Test {
  void SetGainTest(uint8_t gain, uint8_t again_register) {
    // integration_time_ms = 612 for atime = 0x01.
    SetGainAndIntegrationTest(gain, 612, again_register, 0x01);
  }
  void SetIntegrationTest(uint32_t integration_time_ms, uint8_t atime_register) {
    // gain = 1 for again = 0x00.
    SetGainAndIntegrationTest(1, integration_time_ms, 0x00, atime_register);
  }
  void SetGainAndIntegrationTest(uint8_t gain, uint32_t integration_time_ms, uint8_t again_register,
                                 uint8_t atime_register) {
    mock_i2c::MockI2c mock_i2c;
    mock_i2c
        .ExpectWriteStop({0x81, atime_register}, ZX_ERR_INTERNAL)  // error, will retry.
        .ExpectWriteStop({0x81, atime_register}, ZX_ERR_INTERNAL)  // error, will retry.
        .ExpectWriteStop({0x81, atime_register}, ZX_OK)            // integration time (ATIME).
        .ExpectWriteStop({0x8f, again_register});                  // control (for AGAIN).

    std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
    ddk::I2cChannel i2c(mock_i2c.GetProto());
    ddk::GpioProtocolClient gpio;
    zx::port port;
    ASSERT_OK(zx::port::create(0, &port));
    Tcs3400Device device(fake_parent.get(), std::move(i2c), gpio, std::move(port));

    metadata::LightSensorParams parameters = {};
    parameters.integration_time_ms = integration_time_ms;
    parameters.gain = gain;
    fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &parameters,
                             sizeof(metadata::LightSensorParams));
    EXPECT_OK(device.InitMetadata());
    mock_i2c.VerifyAndClear();
  }
};

TEST_F(Tcs3400Test, Gain) {
  SetGainTest(99, 0x00);  // Invalid gain sets again = 0 (gain = 1).
  SetGainTest(1, 0x00);
  SetGainTest(4, 0x01);
  SetGainTest(16, 0x02);
  SetGainTest(64, 0x03);
}

TEST_F(Tcs3400Test, IntegrationTime) {
  SetIntegrationTest(616, 0x01);  // Invalid integration time sets atime = 1.
  SetIntegrationTest(612, 0x01);
  SetIntegrationTest(610, 0x02);
  SetIntegrationTest(608, 0x03);
  SetIntegrationTest(3, 0xFF);
}

TEST(Tcs3400Test, TooManyI2cErrors) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::LightSensorParams parameters = {};
  parameters.gain = 64;
  parameters.integration_time_ms = 612;  // For atime = 0x01.

  mock_i2c::MockI2c mock_i2c;
  mock_i2c
      .ExpectWriteStop({0x81, 0x01}, ZX_ERR_INTERNAL)   // error, will retry.
      .ExpectWriteStop({0x81, 0x01}, ZX_ERR_INTERNAL)   // error, will retry.
      .ExpectWriteStop({0x81, 0x01}, ZX_ERR_INTERNAL);  // error, we are done.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  ddk::GpioProtocolClient gpio;
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));
  Tcs3400Device device(fake_parent.get(), std::move(i2c), gpio, std::move(port));

  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &parameters,
                           sizeof(metadata::LightSensorParams));
  EXPECT_NOT_OK(device.InitMetadata());
}

TEST(Tcs3400Test, InputReport) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::LightSensorParams parameters = {};
  parameters.gain = 64;
  parameters.integration_time_ms = 612;  // For atime = 0x01.

  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &parameters,
                           sizeof(metadata::LightSensorParams));

  ddk::MockGpio mock_gpio;
  mock_gpio.ExpectConfigIn(ZX_OK, GPIO_NO_PULL);
  zx::interrupt irq;
  mock_gpio.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWriteStop({0x81, 0x01});  // integration time (ATIME).
  mock_i2c.ExpectWriteStop({0x8f, 0x03});  // control (for AGAIN).

  // Raw clear.
  mock_i2c.ExpectWrite({0x94}).ExpectReadStop({0x12});
  mock_i2c.ExpectWrite({0x95}).ExpectReadStop({0x34});

  // Raw red.
  mock_i2c.ExpectWrite({0x96}).ExpectReadStop({0xab});
  mock_i2c.ExpectWrite({0x97}).ExpectReadStop({0xcd});

  // Raw green.
  mock_i2c.ExpectWrite({0x98}).ExpectReadStop({0x55});
  mock_i2c.ExpectWrite({0x99}).ExpectReadStop({0x66});

  // Raw Blue.
  mock_i2c.ExpectWrite({0x9a}).ExpectReadStop({0x77});
  mock_i2c.ExpectWrite({0x9b}).ExpectReadStop({0x88});

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  zx::port port;
  ASSERT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));

  struct Tcs3400DeviceTest : public Tcs3400Device {
    Tcs3400DeviceTest(zx_device_t* device, ddk::I2cChannel i2c, ddk::GpioProtocolClient gpio,
                      zx::port port)
        : Tcs3400Device(device, std::move(i2c), gpio, std::move(port)) {}
    void ShutDown() override { Tcs3400Device::ShutDown(); }
  };
  Tcs3400DeviceTest device(fake_parent.get(), std::move(i2c),
                           ddk::GpioProtocolClient(mock_gpio.GetProto()), std::move(port));
  EXPECT_OK(device.InitMetadata());

  ambient_light_input_rpt_t report = {};
  size_t actual = 0;
  EXPECT_OK(device.HidbusGetReport(HID_REPORT_TYPE_INPUT, AMBIENT_LIGHT_RPT_ID_INPUT,
                                   (uint8_t*)&report, sizeof(report), &actual));
  EXPECT_EQ(sizeof(report), actual);

  EXPECT_EQ(AMBIENT_LIGHT_RPT_ID_INPUT, report.rpt_id);

  // Use memcpy() to avoid loading a misaligned pointer in this packed struct.
  uint16_t clear_light = 0;
  memcpy(&clear_light, &report.illuminance, sizeof(clear_light));
  EXPECT_EQ(0x3412, clear_light);
  uint16_t red_light = 0;
  memcpy(&red_light, &report.red, sizeof(red_light));
  EXPECT_EQ(0xcdab, red_light);
  uint16_t green_light = 0;
  memcpy(&green_light, &report.green, sizeof(green_light));
  EXPECT_EQ(0x6655, green_light);
  uint16_t blue_light = 0;
  memcpy(&blue_light, &report.blue, sizeof(blue_light));
  EXPECT_EQ(0x8877, blue_light);

  mock_i2c.VerifyAndClear();
  device.ShutDown();
}

TEST(Tcs3400Test, InputReportSaturated) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::LightSensorParams parameters = {};
  parameters.gain = 64;
  parameters.integration_time_ms = 612;  // For atime = 0x01.

  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &parameters,
                           sizeof(metadata::LightSensorParams));

  ddk::MockGpio mock_gpio;
  mock_gpio.ExpectConfigIn(ZX_OK, GPIO_NO_PULL);
  zx::interrupt irq;
  mock_gpio.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));
  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWriteStop({0x81, 0x01});  // integration time (ATIME).
  mock_i2c.ExpectWriteStop({0x8f, 0x03});  // control (for AGAIN).

  // Raw clear.
  mock_i2c.ExpectWrite({0x94}).ExpectReadStop({0xff});
  mock_i2c.ExpectWrite({0x95}).ExpectReadStop({0xff});

  // Raw red.
  mock_i2c.ExpectWrite({0x96}).ExpectReadStop({0xab});
  mock_i2c.ExpectWrite({0x97}).ExpectReadStop({0xcd});

  // Raw green.
  mock_i2c.ExpectWrite({0x98}).ExpectReadStop({0x55});
  mock_i2c.ExpectWrite({0x99}).ExpectReadStop({0x66});

  // Raw Blue.
  mock_i2c.ExpectWrite({0x9a}).ExpectReadStop({0x77});
  mock_i2c.ExpectWrite({0x9b}).ExpectReadStop({0x88});

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  zx::port port;
  ASSERT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));

  struct Tcs3400DeviceTest : public Tcs3400Device {
    Tcs3400DeviceTest(zx_device_t* device, ddk::I2cChannel i2c, ddk::GpioProtocolClient gpio,
                      zx::port port)
        : Tcs3400Device(device, std::move(i2c), gpio, std::move(port)) {}
    void ShutDown() override { Tcs3400Device::ShutDown(); }
  };
  Tcs3400DeviceTest device(fake_parent.get(), std::move(i2c),
                           ddk::GpioProtocolClient(mock_gpio.GetProto()), std::move(port));
  EXPECT_OK(device.InitMetadata());

  ambient_light_input_rpt_t report = {};
  size_t actual = 0;
  EXPECT_OK(device.HidbusGetReport(HID_REPORT_TYPE_INPUT, AMBIENT_LIGHT_RPT_ID_INPUT,
                                   (uint8_t*)&report, sizeof(report), &actual));
  EXPECT_EQ(sizeof(report), actual);

  EXPECT_EQ(AMBIENT_LIGHT_RPT_ID_INPUT, report.rpt_id);

  // Use memcpy() to avoid loading a misaligned pointer in this packed struct.
  uint16_t clear_light = 0;
  memcpy(&clear_light, &report.illuminance, sizeof(clear_light));
  EXPECT_EQ(65085, clear_light);
  uint16_t red_light = 0;
  memcpy(&red_light, &report.red, sizeof(red_light));
  EXPECT_EQ(21067, red_light);
  uint16_t green_light = 0;
  memcpy(&green_light, &report.green, sizeof(green_light));
  EXPECT_EQ(20395, green_light);
  uint16_t blue_light = 0;
  memcpy(&blue_light, &report.blue, sizeof(blue_light));
  EXPECT_EQ(20939, blue_light);

  mock_i2c.VerifyAndClear();
  device.ShutDown();
}

}  // namespace tcs
