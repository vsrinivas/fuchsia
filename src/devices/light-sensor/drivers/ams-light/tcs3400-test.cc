// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tcs3400.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/ddk/metadata.h>
#include <lib/device-protocol/i2c-channel.h>
#include <lib/fake-i2c/fake-i2c.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/zx/clock.h>

#include <ddktl/metadata/light-sensor.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "tcs3400-regs.h"

namespace tcs {

class FakeLightSensor : public fake_i2c::FakeI2c {
 public:
  uint8_t GetRegisterLastWrite(const uint8_t address) {
    fbl::AutoLock lock(&registers_lock_);
    return registers_[address].size() ? registers_[address].back() : 0;
  }
  uint8_t GetRegisterAtIndex(size_t index, const uint8_t address) {
    fbl::AutoLock lock(&registers_lock_);
    return registers_[address][index];
  }

  void SetRegister(const uint8_t address, const uint8_t value) {
    fbl::AutoLock lock(&registers_lock_);
    registers_[address].push_back(value);
  }

  void WaitForLightDataRead() {
    sync_completion_wait(&read_completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&read_completion_);
  }

  void WaitForConfiguration() {
    sync_completion_wait(&configuration_completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&configuration_completion_);
  }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    if (write_buffer_size < 1) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    const uint8_t address = write_buffer[0];
    write_buffer++;
    write_buffer_size--;

    // Assume that there are no multi-byte register accesses.

    if (write_buffer_size > 1) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    {
      fbl::AutoLock lock(&registers_lock_);
      if (write_buffer_size == 1) {
        registers_[address].push_back(write_buffer[0]);
      }
    }
    read_buffer[0] = GetRegisterLastWrite(address);

    *read_buffer_size = 1;

    // The interrupt or timeout has been received and the driver is reading out the data registers.
    if (address == TCS_I2C_BDATAH) {
      sync_completion_signal(&read_completion_);
    } else if (!first_enable_written_ && address == TCS_I2C_ENABLE) {
      first_enable_written_ = true;
    } else if (first_enable_written_ && address == TCS_I2C_ENABLE) {
      first_enable_written_ = false;
      sync_completion_signal(&configuration_completion_);
    }

    return ZX_OK;
  }

 private:
  fbl::Mutex registers_lock_;
  std::array<std::vector<uint8_t>, UINT8_MAX> registers_ TA_GUARDED(registers_lock_) = {};
  sync_completion_t read_completion_;
  sync_completion_t configuration_completion_;
  bool first_enable_written_ = false;
};

class Tcs3400Test : public zxtest::Test {
 public:
  void SetUp() override {
    constexpr metadata::LightSensorParams kLightSensorMetadata = {
        .gain = 16,
        .integration_time_us = 615'000,
        .polling_time_us = 0,
    };

    fake_parent_ = MockDevice::FakeRootParent();
    fake_parent_->SetMetadata(DEVICE_METADATA_PRIVATE, &kLightSensorMetadata,
                              sizeof(kLightSensorMetadata));

    fake_parent_->AddProtocol(ZX_PROTOCOL_I2C, fake_i2c_.GetProto()->ops, fake_i2c_.GetProto()->ctx,
                              "i2c");
    fake_parent_->AddProtocol(ZX_PROTOCOL_GPIO, mock_gpio_.GetProto()->ops,
                              mock_gpio_.GetProto()->ctx, "gpio");

    ASSERT_OK(zx::interrupt::create(zx::resource(ZX_HANDLE_INVALID), 0, ZX_INTERRUPT_VIRTUAL,
                                    &gpio_interrupt_));

    zx::interrupt gpio_interrupt;
    ASSERT_OK(gpio_interrupt_.duplicate(ZX_RIGHT_SAME_RIGHTS, &gpio_interrupt));

    mock_gpio_.ExpectConfigIn(ZX_OK, GPIO_NO_PULL);
    mock_gpio_.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(gpio_interrupt));

    const auto status = Tcs3400Device::CreateAndGetDevice(nullptr, fake_parent_.get());
    ASSERT_TRUE(status.is_ok());
    device_ = status.value();

    fake_i2c_.WaitForConfiguration();

    EXPECT_EQ(fake_i2c_.GetRegisterLastWrite(TCS_I2C_ATIME), 35);
    EXPECT_EQ(fake_i2c_.GetRegisterLastWrite(TCS_I2C_CONTROL), 0x02);
  }

  void TearDown() override {
    device_async_remove(device_->zxdev());
    EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(fake_parent_.get()));
  }

  fidl::ClientEnd<fuchsia_input_report::InputDevice> FidlClient() {
    fidl::ClientEnd<fuchsia_input_report::InputDevice> client;
    fidl::ServerEnd<fuchsia_input_report::InputDevice> server;
    if (zx::channel::create(0, &client.channel(), &server.channel()) != ZX_OK) {
      return {};
    }

    fidl::BindServer(device_->dispatcher(), std::move(server), device_);
    return client;
  }

 protected:
  static void GetFeatureReport(fidl::WireSyncClient<fuchsia_input_report::InputDevice>& client,
                               Tcs3400FeatureReport* const out_report) {
    const auto response = client->GetFeatureReport();
    ASSERT_TRUE(response.ok());
    ASSERT_FALSE(response->result.is_err());
    ASSERT_TRUE(response->result.response().report.has_sensor());

    const auto& report = response->result.response().report.sensor();
    EXPECT_TRUE(report.has_report_interval());
    ASSERT_TRUE(report.has_reporting_state());

    ASSERT_TRUE(report.has_sensitivity());
    ASSERT_EQ(report.sensitivity().count(), 1);

    ASSERT_TRUE(report.has_threshold_high());
    ASSERT_EQ(report.threshold_high().count(), 1);

    ASSERT_TRUE(report.has_threshold_low());
    ASSERT_EQ(report.threshold_low().count(), 1);

    ASSERT_TRUE(report.has_sampling_rate());

    out_report->report_interval_us = report.report_interval();
    out_report->reporting_state = report.reporting_state();
    out_report->sensitivity = report.sensitivity()[0];
    out_report->threshold_high = report.threshold_high()[0];
    out_report->threshold_low = report.threshold_low()[0];
    out_report->integration_time_us = report.sampling_rate();
  }

  static auto SetFeatureReport(fidl::WireSyncClient<fuchsia_input_report::InputDevice>& client,
                               const Tcs3400FeatureReport& report) {
    fidl::Arena<512> allocator;
    fidl::VectorView<int64_t> sensitivity(allocator, 1);
    sensitivity[0] = report.sensitivity;

    fidl::VectorView<int64_t> threshold_high(allocator, 1);
    threshold_high[0] = report.threshold_high;

    fidl::VectorView<int64_t> threshold_low(allocator, 1);
    threshold_low[0] = report.threshold_low;

    const auto set_sensor_report = fuchsia_input_report::wire::SensorFeatureReport(allocator)
                                       .set_report_interval(allocator, report.report_interval_us)
                                       .set_reporting_state(report.reporting_state)
                                       .set_sensitivity(allocator, sensitivity)
                                       .set_threshold_high(allocator, threshold_high)
                                       .set_threshold_low(allocator, threshold_low)
                                       .set_sampling_rate(allocator, report.integration_time_us);

    const auto set_report = fuchsia_input_report::wire::FeatureReport(allocator).set_sensor(
        allocator, set_sensor_report);

    return client->SetFeatureReport(set_report);
  }

  void SetLightDataRegisters(uint16_t illuminance, uint16_t red, uint16_t green, uint16_t blue) {
    fake_i2c_.SetRegister(TCS_I2C_CDATAL, illuminance & 0xff);
    fake_i2c_.SetRegister(TCS_I2C_CDATAH, illuminance >> 8);

    fake_i2c_.SetRegister(TCS_I2C_RDATAL, red & 0xff);
    fake_i2c_.SetRegister(TCS_I2C_RDATAH, red >> 8);

    fake_i2c_.SetRegister(TCS_I2C_GDATAL, green & 0xff);
    fake_i2c_.SetRegister(TCS_I2C_GDATAH, green >> 8);

    fake_i2c_.SetRegister(TCS_I2C_BDATAL, blue & 0xff);
    fake_i2c_.SetRegister(TCS_I2C_BDATAH, blue >> 8);
  }

  FakeLightSensor fake_i2c_;
  zx::interrupt gpio_interrupt_;
  Tcs3400Device* device_ = nullptr;

 private:
  ddk::MockGpio mock_gpio_;
  std::shared_ptr<MockDevice> fake_parent_;
};

TEST_F(Tcs3400Test, GetInputReport) {
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> client(FidlClient());
  ASSERT_TRUE(client.client_end().is_valid());

  SetLightDataRegisters(0x1772, 0x95fa, 0xb263, 0x2f32);

  constexpr Tcs3400FeatureReport kEnableAllEvents = {
      .report_interval_us = 1'000,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportAllEvents,
      .sensitivity = 16,
      .threshold_high = 0x8000,
      .threshold_low = 0x1000,
      .integration_time_us = 615'000,
  };

  {
    const auto response = SetFeatureReport(client, kEnableAllEvents);
    ASSERT_TRUE(response.ok());
    EXPECT_FALSE(response->result.is_err());
  }

  fake_i2c_.WaitForLightDataRead();

  for (;;) {
    // Wait for the driver's stored values to be updated.
    const auto response = client->GetInputReport(fuchsia_input_report::wire::DeviceType::kSensor);
    ASSERT_TRUE(response.ok());
    if (response->result.is_err()) {
      continue;
    }

    const auto& report = response->result.response().report;

    ASSERT_TRUE(report.has_sensor());
    ASSERT_TRUE(report.sensor().has_values());
    ASSERT_EQ(report.sensor().values().count(), 4);

    EXPECT_EQ(report.sensor().values()[0], 0x1772);
    EXPECT_EQ(report.sensor().values()[1], 0x95fa);
    EXPECT_EQ(report.sensor().values()[2], 0xb263);
    EXPECT_EQ(report.sensor().values()[3], 0x2f32);
    break;
  }

  constexpr Tcs3400FeatureReport kEnableThresholdEvents = {
      .report_interval_us = 0,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportThresholdEvents,
      .sensitivity = 16,
      .threshold_high = 0x8000,
      .threshold_low = 0x1000,
      .integration_time_us = 615'000,
  };

  {
    const auto response = SetFeatureReport(client, kEnableThresholdEvents);
    ASSERT_TRUE(response.ok());
    EXPECT_FALSE(response->result.is_err());
  }

  {
    const auto response = client->GetInputReport(fuchsia_input_report::wire::DeviceType::kSensor);
    ASSERT_TRUE(response.ok());
    // Not supported when only threshold events are enabled.
    EXPECT_TRUE(response->result.is_err());
  }

  constexpr Tcs3400FeatureReport kDisableEvents = {
      .report_interval_us = 0,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportNoEvents,
      .sensitivity = 16,
      .threshold_high = 0x8000,
      .threshold_low = 0x1000,
      .integration_time_us = 615'000,
  };

  {
    const auto response = SetFeatureReport(client, kDisableEvents);
    ASSERT_TRUE(response.ok());
    EXPECT_FALSE(response->result.is_err());
  }

  {
    const auto response = client->GetInputReport(fuchsia_input_report::wire::DeviceType::kSensor);
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response->result.is_err());
  }
}

TEST_F(Tcs3400Test, GetInputReports) {
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> client(FidlClient());
  ASSERT_TRUE(client.client_end().is_valid());

  constexpr Tcs3400FeatureReport kEnableThresholdEvents = {
      .report_interval_us = 0,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportThresholdEvents,
      .sensitivity = 16,
      .threshold_high = 0x8000,
      .threshold_low = 0x1000,
      .integration_time_us = 615'000,
  };

  {
    const auto response = SetFeatureReport(client, kEnableThresholdEvents);
    ASSERT_TRUE(response.ok());
    EXPECT_FALSE(response->result.is_err());
  }

  fidl::ServerEnd<fuchsia_input_report::InputReportsReader> reader_server;
  zx::status reader_client_end = fidl::CreateEndpoints(&reader_server);
  ASSERT_OK(reader_client_end.status_value());
  fidl::WireSyncClient reader = fidl::BindSyncClient(std::move(*reader_client_end));
  client->GetInputReportsReader(std::move(reader_server));
  device_->WaitForNextReader();

  SetLightDataRegisters(0x00f8, 0xe79d, 0xa5e4, 0xfb1b);

  EXPECT_OK(gpio_interrupt_.trigger(0, zx::clock::get_monotonic()));

  // Wait for the driver to read out the data registers. At this point the interrupt has been ack'd
  // and it is safe to trigger again.
  fake_i2c_.WaitForLightDataRead();

  {
    const auto response = reader->ReadInputReports();
    ASSERT_TRUE(response.ok());
    ASSERT_TRUE(response->result.is_response());

    const auto& reports = response->result.response().reports;

    ASSERT_EQ(reports.count(), 1);
    ASSERT_TRUE(reports[0].has_sensor());
    ASSERT_TRUE(reports[0].sensor().has_values());
    ASSERT_EQ(reports[0].sensor().values().count(), 4);

    EXPECT_EQ(reports[0].sensor().values()[0], 0x00f8);
    EXPECT_EQ(reports[0].sensor().values()[1], 0xe79d);
    EXPECT_EQ(reports[0].sensor().values()[2], 0xa5e4);
    EXPECT_EQ(reports[0].sensor().values()[3], 0xfb1b);
  }

  SetLightDataRegisters(0x67f3, 0xbe39, 0x21e9, 0x319a);
  EXPECT_OK(gpio_interrupt_.trigger(0, zx::clock::get_monotonic()));
  fake_i2c_.WaitForLightDataRead();

  SetLightDataRegisters(0xa5df, 0x0101, 0xc776, 0xc531);
  EXPECT_OK(gpio_interrupt_.trigger(0, zx::clock::get_monotonic()));
  fake_i2c_.WaitForLightDataRead();

  // The previous illuminance value did not cross a threshold, so there should only be one report to
  // read out.
  {
    const auto response = reader->ReadInputReports();
    ASSERT_TRUE(response.ok());
    ASSERT_TRUE(response->result.is_response());

    const auto& reports = response->result.response().reports;

    ASSERT_EQ(reports.count(), 1);
    ASSERT_TRUE(reports[0].has_sensor());
    ASSERT_TRUE(reports[0].sensor().has_values());
    ASSERT_EQ(reports[0].sensor().values().count(), 4);

    EXPECT_EQ(reports[0].sensor().values()[0], 0xa5df);
    EXPECT_EQ(reports[0].sensor().values()[1], 0x0101);
    EXPECT_EQ(reports[0].sensor().values()[2], 0xc776);
    EXPECT_EQ(reports[0].sensor().values()[3], 0xc531);
  }

  SetLightDataRegisters(0x1772, 0x95fa, 0xb263, 0x2f32);

  constexpr Tcs3400FeatureReport kEnableAllEvents = {
      .report_interval_us = 1'000,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportAllEvents,
      .sensitivity = 16,
      .threshold_high = 0x8000,
      .threshold_low = 0x1000,
      .integration_time_us = 615'000,
  };

  {
    const auto response = SetFeatureReport(client, kEnableAllEvents);
    ASSERT_TRUE(response.ok());
    EXPECT_FALSE(response->result.is_err());
  }

  for (uint32_t report_count = 0; report_count < 10;) {
    const auto response = reader->ReadInputReports();
    ASSERT_TRUE(response.ok());
    ASSERT_TRUE(response->result.is_response());

    for (const auto& report : response->result.response().reports) {
      ASSERT_TRUE(report.has_sensor());
      ASSERT_TRUE(report.sensor().has_values());
      ASSERT_EQ(report.sensor().values().count(), 4);

      EXPECT_EQ(report.sensor().values()[0], 0x1772);
      EXPECT_EQ(report.sensor().values()[1], 0x95fa);
      EXPECT_EQ(report.sensor().values()[2], 0xb263);
      EXPECT_EQ(report.sensor().values()[3], 0x2f32);
      report_count++;
    }
  }
}

TEST_F(Tcs3400Test, GetMultipleInputReports) {
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> client(FidlClient());
  ASSERT_TRUE(client.client_end().is_valid());

  constexpr Tcs3400FeatureReport kEnableThresholdEvents = {
      .report_interval_us = 0,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportThresholdEvents,
      .sensitivity = 16,
      .threshold_high = 0x8000,
      .threshold_low = 0x1000,
      .integration_time_us = 615'000,
  };

  const auto response = SetFeatureReport(client, kEnableThresholdEvents);
  ASSERT_TRUE(response.ok());
  EXPECT_FALSE(response->result.is_err());

  fake_i2c_.WaitForConfiguration();

  fidl::ServerEnd<fuchsia_input_report::InputReportsReader> reader_server;
  zx::status reader_client_end = fidl::CreateEndpoints(&reader_server);
  ASSERT_OK(reader_client_end.status_value());
  fidl::WireSyncClient reader = fidl::BindSyncClient(std::move(*reader_client_end));
  client->GetInputReportsReader(std::move(reader_server));
  device_->WaitForNextReader();

  constexpr uint16_t kExpectedLightValues[][4] = {
      {0x00f8, 0xe79d, 0xfb1b, 0xa5e4},
      {0x87f3, 0xbe39, 0x319a, 0x21e9},
      {0xa772, 0x95fa, 0x2f32, 0xb263},
  };

  for (const auto& values : kExpectedLightValues) {
    SetLightDataRegisters(values[0], values[1], values[2], values[3]);
    EXPECT_OK(gpio_interrupt_.trigger(0, zx::clock::get_monotonic()));
    fake_i2c_.WaitForLightDataRead();
  }

  for (size_t i = 0; i < std::size(kExpectedLightValues);) {
    const auto response = reader->ReadInputReports();
    ASSERT_TRUE(response.ok());
    ASSERT_TRUE(response->result.is_response());

    for (const auto& report : response->result.response().reports) {
      ASSERT_TRUE(report.has_sensor());
      ASSERT_TRUE(report.sensor().has_values());
      ASSERT_EQ(report.sensor().values().count(), 4);

      EXPECT_EQ(report.sensor().values()[0], kExpectedLightValues[i][0]);
      EXPECT_EQ(report.sensor().values()[1], kExpectedLightValues[i][1]);
      EXPECT_EQ(report.sensor().values()[2], kExpectedLightValues[i][2]);
      EXPECT_EQ(report.sensor().values()[3], kExpectedLightValues[i][3]);
      i++;
    }
  }
}

TEST_F(Tcs3400Test, GetInputReportsMultipleReaders) {
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> client(FidlClient());
  ASSERT_TRUE(client.client_end().is_valid());

  constexpr Tcs3400FeatureReport kEnableThresholdEvents = {
      .report_interval_us = 0,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportThresholdEvents,
      .sensitivity = 16,
      .threshold_high = 0x8000,
      .threshold_low = 0x1000,
      .integration_time_us = 615'000,
  };

  const auto response = SetFeatureReport(client, kEnableThresholdEvents);
  ASSERT_TRUE(response.ok());
  EXPECT_FALSE(response->result.is_err());

  constexpr size_t kReaderCount = 5;

  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> readers[kReaderCount];
  for (auto& reader : readers) {
    fidl::ServerEnd<fuchsia_input_report::InputReportsReader> reader_server;
    zx::status reader_client_end = fidl::CreateEndpoints(&reader_server);
    ASSERT_OK(reader_client_end.status_value());
    reader = fidl::BindSyncClient(std::move(*reader_client_end));
    client->GetInputReportsReader(std::move(reader_server));
    device_->WaitForNextReader();
  }

  SetLightDataRegisters(0x00f8, 0xe79d, 0xa5e4, 0xfb1b);

  EXPECT_OK(gpio_interrupt_.trigger(0, zx::clock::get_monotonic()));

  for (auto& reader : readers) {
    const auto response = reader->ReadInputReports();
    ASSERT_TRUE(response.ok());
    ASSERT_TRUE(response->result.is_response());

    const auto& reports = response->result.response().reports;

    ASSERT_EQ(reports.count(), 1);
    ASSERT_TRUE(reports[0].has_sensor());
    ASSERT_TRUE(reports[0].sensor().has_values());
    ASSERT_EQ(reports[0].sensor().values().count(), 4);

    EXPECT_EQ(reports[0].sensor().values()[0], 0x00f8);
    EXPECT_EQ(reports[0].sensor().values()[1], 0xe79d);
    EXPECT_EQ(reports[0].sensor().values()[2], 0xa5e4);
    EXPECT_EQ(reports[0].sensor().values()[3], 0xfb1b);
  }
}

TEST_F(Tcs3400Test, InputReportSaturated) {
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> client(FidlClient());
  ASSERT_TRUE(client.client_end().is_valid());

  constexpr Tcs3400FeatureReport kEnableThresholdEvents = {
      .report_interval_us = 0,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportAllEvents,
      .sensitivity = 16,
      .threshold_high = 0x8000,
      .threshold_low = 0x1000,
      .integration_time_us = 615'000,
  };

  {
    const auto response = SetFeatureReport(client, kEnableThresholdEvents);
    ASSERT_TRUE(response.ok());
    EXPECT_FALSE(response->result.is_err());
  }

  fidl::ServerEnd<fuchsia_input_report::InputReportsReader> reader_server;
  zx::status reader_client_end = fidl::CreateEndpoints(&reader_server);
  ASSERT_OK(reader_client_end.status_value());
  fidl::WireSyncClient reader = fidl::BindSyncClient(std::move(*reader_client_end));
  client->GetInputReportsReader(std::move(reader_server));
  device_->WaitForNextReader();

  // Set the clear channel to 0xffff to indicate saturation.
  SetLightDataRegisters(0xffff, 0xabcd, 0x5566, 0x7788);

  EXPECT_OK(gpio_interrupt_.trigger(0, zx::clock::get_monotonic()));

  fake_i2c_.WaitForLightDataRead();

  const auto response = reader->ReadInputReports();
  ASSERT_TRUE(response.ok());
  ASSERT_TRUE(response->result.is_response());

  const auto& reports = response->result.response().reports;

  ASSERT_EQ(reports.count(), 1);
  ASSERT_TRUE(reports[0].has_sensor());
  ASSERT_TRUE(reports[0].sensor().has_values());
  ASSERT_EQ(reports[0].sensor().values().count(), 4);

  EXPECT_EQ(reports[0].sensor().values()[0], 65085);
  EXPECT_EQ(reports[0].sensor().values()[1], 21067);
  EXPECT_EQ(reports[0].sensor().values()[2], 20395);
  EXPECT_EQ(reports[0].sensor().values()[3], 20939);
}

TEST_F(Tcs3400Test, GetDescriptor) {
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> client(FidlClient());
  ASSERT_TRUE(client.client_end().is_valid());

  const auto response = client->GetDescriptor();
  ASSERT_TRUE(response.ok());
  ASSERT_TRUE(response->descriptor.has_device_info());
  ASSERT_TRUE(response->descriptor.has_sensor());
  ASSERT_TRUE(response->descriptor.sensor().has_input());
  ASSERT_EQ(response->descriptor.sensor().input().values().count(), 4);

  EXPECT_EQ(response->descriptor.device_info().vendor_id,
            static_cast<uint32_t>(fuchsia_input_report::wire::VendorId::kGoogle));
  EXPECT_EQ(
      response->descriptor.device_info().product_id,
      static_cast<uint32_t>(fuchsia_input_report::wire::VendorGoogleProductId::kAmsLightSensor));

  const auto& sensor_axes = response->descriptor.sensor().input().values();
  EXPECT_EQ(sensor_axes[0].type, fuchsia_input_report::wire::SensorType::kLightIlluminance);
  EXPECT_EQ(sensor_axes[1].type, fuchsia_input_report::wire::SensorType::kLightRed);
  EXPECT_EQ(sensor_axes[2].type, fuchsia_input_report::wire::SensorType::kLightGreen);
  EXPECT_EQ(sensor_axes[3].type, fuchsia_input_report::wire::SensorType::kLightBlue);

  for (const auto& axis : sensor_axes) {
    EXPECT_EQ(axis.axis.range.min, 0);
    EXPECT_EQ(axis.axis.range.max, UINT16_MAX);
    EXPECT_EQ(axis.axis.unit.type, fuchsia_input_report::wire::UnitType::kOther);
    EXPECT_EQ(axis.axis.unit.exponent, 0);
  }

  ASSERT_TRUE(response->descriptor.sensor().has_feature());
  const auto& feature_descriptor = response->descriptor.sensor().feature();

  ASSERT_TRUE(feature_descriptor.has_report_interval());
  ASSERT_TRUE(feature_descriptor.has_supports_reporting_state());

  ASSERT_TRUE(feature_descriptor.has_sensitivity());
  ASSERT_EQ(feature_descriptor.sensitivity().count(), 1);

  ASSERT_TRUE(feature_descriptor.has_threshold_high());
  ASSERT_EQ(feature_descriptor.threshold_high().count(), 1);

  ASSERT_TRUE(feature_descriptor.has_threshold_low());
  ASSERT_EQ(feature_descriptor.threshold_low().count(), 1);

  EXPECT_EQ(feature_descriptor.report_interval().range.min, 0);
  EXPECT_EQ(feature_descriptor.report_interval().unit.type,
            fuchsia_input_report::wire::UnitType::kSeconds);
  EXPECT_EQ(feature_descriptor.report_interval().unit.exponent, -6);

  EXPECT_TRUE(feature_descriptor.supports_reporting_state());

  EXPECT_EQ(feature_descriptor.sensitivity()[0].type,
            fuchsia_input_report::wire::SensorType::kLightIlluminance);
  EXPECT_EQ(feature_descriptor.sensitivity()[0].axis.range.min, 1);
  EXPECT_EQ(feature_descriptor.sensitivity()[0].axis.range.max, 64);
  EXPECT_EQ(feature_descriptor.sensitivity()[0].axis.unit.type,
            fuchsia_input_report::wire::UnitType::kOther);
  EXPECT_EQ(feature_descriptor.sensitivity()[0].axis.unit.exponent, 0);

  EXPECT_EQ(feature_descriptor.threshold_high()[0].type,
            fuchsia_input_report::wire::SensorType::kLightIlluminance);
  EXPECT_EQ(feature_descriptor.threshold_high()[0].axis.range.min, 0);
  EXPECT_EQ(feature_descriptor.threshold_high()[0].axis.range.max, UINT16_MAX);
  EXPECT_EQ(feature_descriptor.threshold_high()[0].axis.unit.type,
            fuchsia_input_report::wire::UnitType::kOther);
  EXPECT_EQ(feature_descriptor.threshold_high()[0].axis.unit.exponent, 0);

  EXPECT_EQ(feature_descriptor.threshold_low()[0].type,
            fuchsia_input_report::wire::SensorType::kLightIlluminance);
  EXPECT_EQ(feature_descriptor.threshold_low()[0].axis.range.min, 0);
  EXPECT_EQ(feature_descriptor.threshold_low()[0].axis.range.max, UINT16_MAX);
  EXPECT_EQ(feature_descriptor.threshold_low()[0].axis.unit.type,
            fuchsia_input_report::wire::UnitType::kOther);
  EXPECT_EQ(feature_descriptor.threshold_low()[0].axis.unit.exponent, 0);
}

TEST_F(Tcs3400Test, FeatureReport) {
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> client(FidlClient());
  ASSERT_TRUE(client.client_end().is_valid());

  Tcs3400FeatureReport report;
  ASSERT_NO_FATAL_FAILURE(GetFeatureReport(client, &report));

  // Check the default report values.
  EXPECT_EQ(report.reporting_state,
            fuchsia_input_report::wire::SensorReportingState::kReportAllEvents);
  EXPECT_EQ(report.threshold_high, 0xffff);
  EXPECT_EQ(report.threshold_low, 0x0000);
  EXPECT_EQ(report.integration_time_us, 614'380);

  // These values are passed in through metadata.
  EXPECT_EQ(report.report_interval_us, 0);
  EXPECT_EQ(report.sensitivity, 16);

  fake_i2c_.SetRegister(TCS_I2C_ENABLE, 0);
  fake_i2c_.SetRegister(TCS_I2C_AILTL, 0);
  fake_i2c_.SetRegister(TCS_I2C_AILTH, 0);
  fake_i2c_.SetRegister(TCS_I2C_AIHTL, 0);
  fake_i2c_.SetRegister(TCS_I2C_AIHTH, 0);
  fake_i2c_.SetRegister(TCS_I2C_PERS, 0);
  fake_i2c_.SetRegister(TCS_I2C_CONTROL, 0);
  fake_i2c_.SetRegister(TCS_I2C_ATIME, 0);

  constexpr Tcs3400FeatureReport kNewFeatureReport = {
      .report_interval_us = 1'000,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportAllEvents,
      .sensitivity = 64,
      .threshold_high = 0xabcd,
      .threshold_low = 0x1234,
      .integration_time_us = 278'000,
  };
  const auto response = SetFeatureReport(client, kNewFeatureReport);
  ASSERT_TRUE(response.ok());
  EXPECT_FALSE(response->result.is_err());

  fake_i2c_.WaitForConfiguration();

  EXPECT_EQ(fake_i2c_.GetRegisterAtIndex(0, TCS_I2C_ENABLE), 0b0001'0001);
  EXPECT_EQ(fake_i2c_.GetRegisterLastWrite(TCS_I2C_AILTL), 0x34);
  EXPECT_EQ(fake_i2c_.GetRegisterLastWrite(TCS_I2C_AILTH), 0x12);
  EXPECT_EQ(fake_i2c_.GetRegisterLastWrite(TCS_I2C_AIHTL), 0xcd);
  EXPECT_EQ(fake_i2c_.GetRegisterLastWrite(TCS_I2C_AIHTH), 0xab);
  EXPECT_EQ(fake_i2c_.GetRegisterLastWrite(TCS_I2C_CONTROL), 3);
  EXPECT_EQ(fake_i2c_.GetRegisterLastWrite(TCS_I2C_ATIME), 156);
  EXPECT_EQ(fake_i2c_.GetRegisterAtIndex(1, TCS_I2C_ENABLE), 0b0001'0011);

  ASSERT_NO_FATAL_FAILURE(GetFeatureReport(client, &report));
  EXPECT_EQ(report.report_interval_us, 1'000);
  EXPECT_EQ(report.reporting_state,
            fuchsia_input_report::wire::SensorReportingState::kReportAllEvents);
  EXPECT_EQ(report.sensitivity, 64);
  EXPECT_EQ(report.threshold_high, 0xabcd);
  EXPECT_EQ(report.threshold_low, 0x1234);
  EXPECT_EQ(report.integration_time_us, 278'000);
}

TEST_F(Tcs3400Test, SetInvalidFeatureReport) {
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> client(FidlClient());
  ASSERT_TRUE(client.client_end().is_valid());

  constexpr Tcs3400FeatureReport kInvalidReportInterval = {
      .report_interval_us = -1,
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportAllEvents,
      .sensitivity = 1,
  };

  {
    const auto response = SetFeatureReport(client, kInvalidReportInterval);
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response->result.is_err());
  }

  Tcs3400FeatureReport report;
  ASSERT_NO_FATAL_FAILURE(GetFeatureReport(client, &report));
  // Make sure the feature report wasn't affected by the bad call.
  EXPECT_EQ(report.sensitivity, 16);
  EXPECT_EQ(report.report_interval_us, 0);

  constexpr Tcs3400FeatureReport kInvalidSensitivity = {
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportAllEvents,
      .sensitivity = 50,
  };

  {
    const auto response = SetFeatureReport(client, kInvalidSensitivity);
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response->result.is_err());
  }

  ASSERT_NO_FATAL_FAILURE(GetFeatureReport(client, &report));
  EXPECT_EQ(report.sensitivity, 16);

  constexpr Tcs3400FeatureReport kInvalidThresholdHigh = {
      .reporting_state = fuchsia_input_report::wire::SensorReportingState::kReportAllEvents,
      .sensitivity = 1,
      .threshold_high = 0x10000,
  };

  {
    const auto response = SetFeatureReport(client, kInvalidThresholdHigh);
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response->result.is_err());
  }

  ASSERT_NO_FATAL_FAILURE(GetFeatureReport(client, &report));
  EXPECT_EQ(report.threshold_high, 0xffff);
  EXPECT_EQ(report.sensitivity, 16);

  // Make sure the call fails if a field is omitted.
  fidl::Arena<512> allocator;
  fidl::VectorView<int64_t> sensitivity(allocator, 1);
  sensitivity[0] = 1;

  fidl::VectorView<int64_t> threshold_high(allocator, 1);
  threshold_high[0] = 0;

  const auto set_sensor_report =
      fuchsia_input_report::wire::SensorFeatureReport(allocator)
          .set_report_interval(allocator, report.report_interval_us)
          .set_reporting_state(fuchsia_input_report::wire::SensorReportingState::kReportAllEvents)
          .set_sensitivity(allocator, sensitivity)
          .set_threshold_high(allocator, threshold_high);

  const auto set_report =
      fuchsia_input_report::wire::FeatureReport(allocator).set_sensor(allocator, set_sensor_report);

  {
    const auto response = client->SetFeatureReport(set_report);
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response->result.is_err());
  }

  ASSERT_NO_FATAL_FAILURE(GetFeatureReport(client, &report));
  EXPECT_EQ(report.threshold_high, 0xffff);
  EXPECT_EQ(report.threshold_low, 0x0000);
  EXPECT_EQ(report.sensitivity, 16);
  EXPECT_EQ(report.report_interval_us, 0);
  EXPECT_EQ(report.reporting_state,
            fuchsia_input_report::wire::SensorReportingState::kReportAllEvents);
}

class Tcs3400MetadataTest : public zxtest::Test {
 protected:
  void SetGainTest(uint8_t gain, uint8_t again_register) {
    // integration_time_us = 612'000 for atime = 36.
    SetGainAndIntegrationTest(gain, 612'000, again_register, 36);
  }

  void SetIntegrationTest(uint32_t integration_time_us, uint8_t atime_register) {
    // gain = 1 for again = 0x00.
    SetGainAndIntegrationTest(1, integration_time_us, 0x00, atime_register);
  }

  void SetGainAndIntegrationTest(uint8_t gain, uint32_t integration_time_us, uint8_t again_register,
                                 uint8_t atime_register) {
    const metadata::LightSensorParams metadata = {
        .gain = gain,
        .integration_time_us = integration_time_us,
    };

    std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();
    fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

    FakeLightSensor fake_i2c;
    ddk::MockGpio mock_gpio;

    fake_parent->AddProtocol(ZX_PROTOCOL_I2C, fake_i2c.GetProto()->ops, fake_i2c.GetProto()->ctx,
                             "i2c");
    fake_parent->AddProtocol(ZX_PROTOCOL_GPIO, mock_gpio.GetProto()->ops, mock_gpio.GetProto()->ctx,
                             "gpio");

    zx::interrupt gpio_interrupt;
    ASSERT_OK(zx::interrupt::create(zx::resource(ZX_HANDLE_INVALID), 0, ZX_INTERRUPT_VIRTUAL,
                                    &gpio_interrupt));

    zx::interrupt gpio_interrupt_dup;
    ASSERT_OK(gpio_interrupt.duplicate(ZX_RIGHT_SAME_RIGHTS, &gpio_interrupt_dup));

    mock_gpio.ExpectConfigIn(ZX_OK, GPIO_NO_PULL);
    mock_gpio.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(gpio_interrupt_dup));

    fake_i2c.SetRegister(TCS_I2C_ATIME, 0xff);
    fake_i2c.SetRegister(TCS_I2C_CONTROL, 0xff);

    const auto status = Tcs3400Device::CreateAndGetDevice(nullptr, fake_parent.get());
    ASSERT_TRUE(status.is_ok());

    fake_i2c.WaitForConfiguration();

    EXPECT_EQ(fake_i2c.GetRegisterLastWrite(TCS_I2C_ATIME), atime_register);
    EXPECT_EQ(fake_i2c.GetRegisterLastWrite(TCS_I2C_CONTROL), again_register);

    device_async_remove(status.value()->zxdev());
    EXPECT_OK(mock_ddk::ReleaseFlaggedDevices(fake_parent.get()));
  }
};

TEST_F(Tcs3400MetadataTest, Gain) {
  SetGainTest(99, 0x00);  // Invalid gain sets again = 0 (gain = 1).
  SetGainTest(1, 0x00);
  SetGainTest(4, 0x01);
  SetGainTest(16, 0x02);
  SetGainTest(64, 0x03);
}

TEST_F(Tcs3400MetadataTest, IntegrationTime) {
  SetIntegrationTest(750'000, 0x01);  // Invalid integration time sets atime = 1.
  SetIntegrationTest(708'900, 0x01);
  SetIntegrationTest(706'120, 0x02);
  SetIntegrationTest(703'340, 0x03);
  SetIntegrationTest(2'780, 0xFF);
}

TEST(Tcs3400Test, TooManyI2cErrors) {
  std::shared_ptr<MockDevice> fake_parent = MockDevice::FakeRootParent();

  metadata::LightSensorParams parameters = {};
  parameters.gain = 64;
  parameters.integration_time_us = 708'900;  // For atime = 0x01.

  mock_i2c::MockI2c mock_i2c;
  mock_i2c
      .ExpectWriteStop({0x81, 0x01}, ZX_ERR_INTERNAL)   // error, will retry.
      .ExpectWriteStop({0x81, 0x01}, ZX_ERR_INTERNAL)   // error, will retry.
      .ExpectWriteStop({0x81, 0x01}, ZX_ERR_INTERNAL);  // error, we are done.

  ddk::I2cChannel i2c(mock_i2c.GetProto());
  ddk::GpioProtocolClient gpio;
  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));
  Tcs3400Device device(fake_parent.get(), i2c, gpio, std::move(port));

  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &parameters,
                           sizeof(metadata::LightSensorParams));
  EXPECT_NOT_OK(device.InitMetadata());
}

}  // namespace tcs
