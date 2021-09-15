// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/input_device.h"

#include <fidl/fuchsia.hardware.goldfish/cpp/wire.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/driver.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/zx/time.h>

#include <cmath>

#include <gtest/gtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/input/lib/input-report-reader/reader.h"

namespace goldfish::sensor {

template <class DutType>
class InputDeviceTest : public ::testing::Test {
 public:
  void SetUp() override {
    fake_parent_ = MockDevice::FakeRootParent();

    loop_ = std::make_unique<async::TestLoop>();
    auto device = std::make_unique<DutType>(fake_parent_.get(), loop_->dispatcher());
    ASSERT_EQ(device->DdkAdd("goldfish-sensor-input"), ZX_OK);
    // dut_ will be deleted by MockDevice when the test ends.
    dut_ = device.release();

    ASSERT_EQ(fake_parent_->child_count(), 1u);

    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputDevice>();
    ASSERT_TRUE(endpoints.is_ok());

    binding_ = fidl::BindServer(loop_->dispatcher(), std::move(endpoints->server),
                                fake_parent_->GetLatestChild()->GetDeviceContext<InputDevice>());

    device_client_ = fidl::WireClient(std::move(endpoints->client), loop_->dispatcher());
  }

  void TearDown() override {
    // After tests finishes, there might still be some pending events on the
    // loop that uses the InputReportsReader which could cause a use-after-free
    // error if we destroy the loop after destroying the device. Thus we destroy
    // the loop first. This is safe as long as we don't call
    // GetInputReportsReader() after resetting |loop_|.
    loop_.reset();

    device_async_remove(dut_->zxdev());
    mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
  }

  DutType* dut() const { return static_cast<DutType*>(dut_); }

 protected:
  std::shared_ptr<MockDevice> fake_parent_;
  std::unique_ptr<async::TestLoop> loop_;
  InputDevice* dut_;
  fidl::WireClient<fuchsia_input_report::InputDevice> device_client_;
  std::optional<fidl::ServerBindingRef<fuchsia_input_report::InputDevice>> binding_;
};

class TestAccelerationInputDevice : public AccelerationInputDevice {
 public:
  explicit TestAccelerationInputDevice(zx_device_t* parent, async_dispatcher_t* dispatcher)
      : AccelerationInputDevice(parent, dispatcher, nullptr) {}

  ~TestAccelerationInputDevice() override = default;

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override {
    AccelerationInputDevice::GetInputReportsReader(request, completer);
    ++readers_created_;
  }
  size_t readers_created() const { return readers_created_.load(); }

 private:
  // For test purpose only.
  std::atomic<size_t> readers_created_ = 0;
};

using AccelerationInputDeviceTest = InputDeviceTest<TestAccelerationInputDevice>;

TEST_F(AccelerationInputDeviceTest, ReadInputReports) {
  auto endpoint_result = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  ASSERT_TRUE(endpoint_result.is_ok());
  auto [client_end, server_end] = std::move(endpoint_result.value());

  auto reader_result = device_client_->GetInputReportsReader(std::move(server_end));
  ASSERT_TRUE(reader_result.ok());
  auto reader_client = fidl::WireClient<fuchsia_input_report::InputReportsReader>(
      std::move(client_end), loop_->dispatcher());

  ASSERT_TRUE(loop_->RunUntilIdle());

  // The FIDL callback runs on another thread by fake_ddk::FidlMessenger.
  // We will need to wait for the FIDL callback to finish before using |client|.
  ASSERT_TRUE(dut()->readers_created());

  SensorReport rpt = {.name = "acceleration", .data = {Numeric(1.0), Numeric(2.0), Numeric(3.0)}};
  EXPECT_EQ(dut_->OnReport(rpt), ZX_OK);

  reader_client->ReadInputReports(
      [](fidl::WireUnownedResult<fuchsia_input_report::InputReportsReader::ReadInputReports>&
             result) {
        ASSERT_TRUE(result.ok());
        auto* response = result.Unwrap();
        ASSERT_TRUE(response->result.is_response());
        auto& reports = response->result.response().reports;
        ASSERT_EQ(reports.count(), 1u);
        auto& report = response->result.response().reports[0];
        ASSERT_TRUE(report.has_sensor());
        auto& sensor = response->result.response().reports[0].sensor();
        ASSERT_TRUE(sensor.has_values() && sensor.values().count() == 3);
        EXPECT_EQ(sensor.values().at(0), 100);
        EXPECT_EQ(sensor.values().at(1), 200);
        EXPECT_EQ(sensor.values().at(2), 300);
      });
  ASSERT_TRUE(loop_->RunUntilIdle());
}

TEST_F(AccelerationInputDeviceTest, Descriptor) {
  bool get_descriptor_called = false;
  device_client_->GetDescriptor(
      [&get_descriptor_called](
          fidl::WireUnownedResult<fuchsia_input_report::InputDevice::GetDescriptor>& result) {
        ASSERT_TRUE(result.ok());
        auto* response = result.Unwrap();
        get_descriptor_called = true;
        ASSERT_NE(response, nullptr);
        const auto& descriptor = response->descriptor;

        ASSERT_TRUE(descriptor.has_sensor());
        EXPECT_FALSE(descriptor.has_keyboard());
        EXPECT_FALSE(descriptor.has_mouse());
        EXPECT_FALSE(descriptor.has_touch());
        EXPECT_FALSE(descriptor.has_consumer_control());

        ASSERT_TRUE(descriptor.sensor().has_input());
        ASSERT_TRUE(descriptor.sensor().input().has_values());
        const auto& values = descriptor.sensor().input().values();

        ASSERT_EQ(values.count(), 3u);
        EXPECT_EQ(values[0].type, fuchsia_input_report::wire::SensorType::kAccelerometerX);
        EXPECT_EQ(values[1].type, fuchsia_input_report::wire::SensorType::kAccelerometerY);
        EXPECT_EQ(values[2].type, fuchsia_input_report::wire::SensorType::kAccelerometerZ);

        EXPECT_EQ(values[0].axis.unit.type,
                  fuchsia_input_report::wire::UnitType::kSiLinearAcceleration);
        EXPECT_EQ(values[1].axis.unit.type,
                  fuchsia_input_report::wire::UnitType::kSiLinearAcceleration);
        EXPECT_EQ(values[2].axis.unit.type,
                  fuchsia_input_report::wire::UnitType::kSiLinearAcceleration);

        EXPECT_EQ(values[0].axis.unit.exponent, -2);
        EXPECT_EQ(values[1].axis.unit.exponent, -2);
        EXPECT_EQ(values[2].axis.unit.exponent, -2);
      });
  ASSERT_TRUE(loop_->RunUntilIdle());
  EXPECT_TRUE(get_descriptor_called);
}

TEST_F(AccelerationInputDeviceTest, InvalidInputReports) {
  // Invalid number of elements.
  SensorReport invalid_report1 = {.name = "acceleration", .data = {Numeric(1.0), Numeric(2.0)}};
  EXPECT_EQ(dut_->OnReport(invalid_report1), ZX_ERR_INVALID_ARGS);

  // Invalid x.
  SensorReport invalid_report2 = {.name = "acceleration",
                                  .data = {"string", Numeric(2.0), Numeric(3.0)}};
  EXPECT_EQ(dut_->OnReport(invalid_report2), ZX_ERR_INVALID_ARGS);

  // Invalid y.
  SensorReport invalid_report3 = {.name = "acceleration",
                                  .data = {Numeric(2.0), "string", Numeric(3.0)}};
  EXPECT_EQ(dut_->OnReport(invalid_report3), ZX_ERR_INVALID_ARGS);

  // Invalid z.
  SensorReport invalid_report4 = {.name = "acceleration",
                                  .data = {Numeric(2.0), Numeric(3.0), "string"}};
  EXPECT_EQ(dut_->OnReport(invalid_report4), ZX_ERR_INVALID_ARGS);
}

class TestGyroscopeInputDevice : public GyroscopeInputDevice {
 public:
  explicit TestGyroscopeInputDevice(zx_device_t* parent, async_dispatcher_t* dispatcher)
      : GyroscopeInputDevice(parent, dispatcher, nullptr) {}

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override {
    GyroscopeInputDevice::GetInputReportsReader(request, completer);
    ++readers_created_;
  }
  size_t readers_created() const { return readers_created_.load(); }

 private:
  // For test purpose only.
  std::atomic<size_t> readers_created_ = 0;
};

using GyroscopeInputDeviceTest = InputDeviceTest<TestGyroscopeInputDevice>;

TEST_F(GyroscopeInputDeviceTest, ReadInputReports) {
  auto endpoint_result = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  ASSERT_TRUE(endpoint_result.is_ok());
  auto [client_end, server_end] = std::move(endpoint_result.value());

  auto reader_result = device_client_->GetInputReportsReader(std::move(server_end));
  ASSERT_TRUE(reader_result.ok());
  auto reader_client = fidl::WireClient<fuchsia_input_report::InputReportsReader>(
      std::move(client_end), loop_->dispatcher());

  ASSERT_TRUE(loop_->RunUntilIdle());

  // The FIDL callback runs on another thread by fake_ddk::FidlMessenger.
  // We will need to wait for the FIDL callback to finish before using |client|.
  ASSERT_TRUE(dut()->readers_created());

  SensorReport rpt = {.name = "gyroscope",
                      .data = {Numeric(M_PI), Numeric(2.0 * M_PI), Numeric(3.0 * M_PI)}};
  EXPECT_EQ(dut_->OnReport(rpt), ZX_OK);

  reader_client->ReadInputReports(
      [](fidl::WireUnownedResult<fuchsia_input_report::InputReportsReader::ReadInputReports>&
             result) {
        ASSERT_TRUE(result.ok());
        auto* response = result.Unwrap();
        ASSERT_TRUE(response->result.is_response());
        auto& reports = response->result.response().reports;
        ASSERT_EQ(reports.count(), 1u);
        auto& report = response->result.response().reports[0];
        ASSERT_TRUE(report.has_sensor());
        auto& sensor = response->result.response().reports[0].sensor();
        ASSERT_TRUE(sensor.has_values() && sensor.values().count() == 3);
        EXPECT_EQ(sensor.values().at(0), 18000);
        EXPECT_EQ(sensor.values().at(1), 36000);
        EXPECT_EQ(sensor.values().at(2), 54000);
      });
  ASSERT_TRUE(loop_->RunUntilIdle());
}

TEST_F(GyroscopeInputDeviceTest, Descriptor) {
  bool get_descriptor_called = false;
  device_client_->GetDescriptor(
      [&get_descriptor_called](
          fidl::WireUnownedResult<fuchsia_input_report::InputDevice::GetDescriptor>& result) {
        ASSERT_TRUE(result.ok());
        auto* response = result.Unwrap();

        get_descriptor_called = true;
        ASSERT_NE(response, nullptr);
        const auto& descriptor = response->descriptor;

        ASSERT_TRUE(descriptor.has_sensor());
        EXPECT_FALSE(descriptor.has_keyboard());
        EXPECT_FALSE(descriptor.has_mouse());
        EXPECT_FALSE(descriptor.has_touch());
        EXPECT_FALSE(descriptor.has_consumer_control());

        ASSERT_TRUE(descriptor.sensor().has_input());
        ASSERT_TRUE(descriptor.sensor().input().has_values());
        const auto& values = descriptor.sensor().input().values();

        ASSERT_EQ(values.count(), 3u);
        EXPECT_EQ(values[0].type, fuchsia_input_report::wire::SensorType::kGyroscopeX);
        EXPECT_EQ(values[1].type, fuchsia_input_report::wire::SensorType::kGyroscopeY);
        EXPECT_EQ(values[2].type, fuchsia_input_report::wire::SensorType::kGyroscopeZ);

        EXPECT_EQ(values[0].axis.unit.type,
                  fuchsia_input_report::wire::UnitType::kEnglishAngularVelocity);
        EXPECT_EQ(values[1].axis.unit.type,
                  fuchsia_input_report::wire::UnitType::kEnglishAngularVelocity);
        EXPECT_EQ(values[2].axis.unit.type,
                  fuchsia_input_report::wire::UnitType::kEnglishAngularVelocity);

        EXPECT_EQ(values[0].axis.unit.exponent, -2);
        EXPECT_EQ(values[1].axis.unit.exponent, -2);
        EXPECT_EQ(values[2].axis.unit.exponent, -2);
      });
  ASSERT_TRUE(loop_->RunUntilIdle());
  EXPECT_TRUE(get_descriptor_called);
}

TEST_F(GyroscopeInputDeviceTest, InvalidInputReports) {
  // Invalid number of elements.
  SensorReport invalid_report1 = {.name = "gyroscope", .data = {Numeric(1.0), Numeric(2.0)}};
  EXPECT_EQ(dut_->OnReport(invalid_report1), ZX_ERR_INVALID_ARGS);

  // Invalid x.
  SensorReport invalid_report2 = {.name = "gyroscope",
                                  .data = {"string", Numeric(2.0), Numeric(3.0)}};
  EXPECT_EQ(dut_->OnReport(invalid_report2), ZX_ERR_INVALID_ARGS);

  // Invalid y.
  SensorReport invalid_report3 = {.name = "gyroscope",
                                  .data = {Numeric(2.0), "string", Numeric(3.0)}};
  EXPECT_EQ(dut_->OnReport(invalid_report3), ZX_ERR_INVALID_ARGS);

  // Invalid z.
  SensorReport invalid_report4 = {.name = "gyroscope",
                                  .data = {Numeric(2.0), Numeric(3.0), "string"}};
  EXPECT_EQ(dut_->OnReport(invalid_report4), ZX_ERR_INVALID_ARGS);
}

class TestRgbcLightInputDevice : public RgbcLightInputDevice {
 public:
  explicit TestRgbcLightInputDevice(zx_device_t* parent, async_dispatcher_t* dispatcher)
      : RgbcLightInputDevice(parent, dispatcher, nullptr) {}

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override {
    RgbcLightInputDevice::GetInputReportsReader(request, completer);
    ++readers_created_;
  }
  size_t readers_created() const { return readers_created_.load(); }

 private:
  // For test purpose only.
  std::atomic<size_t> readers_created_ = 0;
};

using RgbcLightInputDeviceTest = InputDeviceTest<TestRgbcLightInputDevice>;

TEST_F(RgbcLightInputDeviceTest, ReadInputReports) {
  auto endpoint_result = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  ASSERT_TRUE(endpoint_result.is_ok());
  auto [client_end, server_end] = std::move(endpoint_result.value());

  auto reader_result = device_client_->GetInputReportsReader(std::move(server_end));
  ASSERT_TRUE(reader_result.ok());
  auto reader_client = fidl::WireClient<fuchsia_input_report::InputReportsReader>(
      std::move(client_end), loop_->dispatcher());

  ASSERT_TRUE(loop_->RunUntilIdle());

  // The FIDL callback runs on another thread by fake_ddk::FidlMessenger.
  // We will need to wait for the FIDL callback to finish before using |client|.
  ASSERT_TRUE(dut()->readers_created());

  SensorReport rpt = {.name = "rgbclight",
                      .data = {Numeric(100L), Numeric(200L), Numeric(300L), Numeric(400L)}};
  EXPECT_EQ(dut_->OnReport(rpt), ZX_OK);

  reader_client->ReadInputReports(
      [](fidl::WireUnownedResult<fuchsia_input_report::InputReportsReader::ReadInputReports>&
             result) {
        ASSERT_TRUE(result.ok());
        auto* response = result.Unwrap();
        ASSERT_TRUE(response->result.is_response());
        auto& reports = response->result.response().reports;
        ASSERT_EQ(reports.count(), 1u);
        auto& report = response->result.response().reports[0];
        ASSERT_TRUE(report.has_sensor());
        auto& sensor = response->result.response().reports[0].sensor();
        ASSERT_TRUE(sensor.has_values() && sensor.values().count() == 4);
        EXPECT_EQ(sensor.values().at(0), 100L);
        EXPECT_EQ(sensor.values().at(1), 200L);
        EXPECT_EQ(sensor.values().at(2), 300L);
        EXPECT_EQ(sensor.values().at(3), 400L);
      });
  ASSERT_TRUE(loop_->RunUntilIdle());
}

TEST_F(RgbcLightInputDeviceTest, Descriptor) {
  bool get_descriptor_called = false;
  device_client_->GetDescriptor(
      [&get_descriptor_called](
          fidl::WireUnownedResult<fuchsia_input_report::InputDevice::GetDescriptor>& result) {
        ASSERT_TRUE(result.ok());
        auto* response = result.Unwrap();
        get_descriptor_called = true;
        ASSERT_NE(response, nullptr);
        const auto& descriptor = response->descriptor;

        ASSERT_TRUE(descriptor.has_sensor());
        EXPECT_FALSE(descriptor.has_keyboard());
        EXPECT_FALSE(descriptor.has_mouse());
        EXPECT_FALSE(descriptor.has_touch());
        EXPECT_FALSE(descriptor.has_consumer_control());

        ASSERT_TRUE(descriptor.sensor().has_input());
        ASSERT_TRUE(descriptor.sensor().input().has_values());
        const auto& values = descriptor.sensor().input().values();

        ASSERT_EQ(values.count(), 4u);
        EXPECT_EQ(values[0].type, fuchsia_input_report::wire::SensorType::kLightRed);
        EXPECT_EQ(values[1].type, fuchsia_input_report::wire::SensorType::kLightGreen);
        EXPECT_EQ(values[2].type, fuchsia_input_report::wire::SensorType::kLightBlue);
        EXPECT_EQ(values[3].type, fuchsia_input_report::wire::SensorType::kLightIlluminance);

        EXPECT_EQ(values[0].axis.unit.type, fuchsia_input_report::wire::UnitType::kNone);
        EXPECT_EQ(values[1].axis.unit.type, fuchsia_input_report::wire::UnitType::kNone);
        EXPECT_EQ(values[2].axis.unit.type, fuchsia_input_report::wire::UnitType::kNone);
        EXPECT_EQ(values[3].axis.unit.type, fuchsia_input_report::wire::UnitType::kNone);
      });
  ASSERT_TRUE(loop_->RunUntilIdle());
  EXPECT_TRUE(get_descriptor_called);
}

TEST_F(RgbcLightInputDeviceTest, InvalidInputReports) {
  // Invalid number of elements.
  SensorReport invalid_report1 = {.name = "rgbc-light",
                                  .data = {Numeric(1.0), Numeric(2.0), Numeric(3.0)}};
  EXPECT_EQ(dut_->OnReport(invalid_report1), ZX_ERR_INVALID_ARGS);

  // Invalid r.
  SensorReport invalid_report2 = {.name = "rgbc-light",
                                  .data = {"string", Numeric(100L), Numeric(200L), Numeric(300L)}};
  EXPECT_EQ(dut_->OnReport(invalid_report2), ZX_ERR_INVALID_ARGS);

  // Invalid g.
  SensorReport invalid_report3 = {.name = "rgbc-light",
                                  .data = {Numeric(100L), "string", Numeric(200L), Numeric(300L)}};
  EXPECT_EQ(dut_->OnReport(invalid_report3), ZX_ERR_INVALID_ARGS);

  // Invalid b.
  SensorReport invalid_report4 = {.name = "rgbc-light",
                                  .data = {Numeric(100L), Numeric(200L), "string", Numeric(300L)}};
  EXPECT_EQ(dut_->OnReport(invalid_report4), ZX_ERR_INVALID_ARGS);

  // Invalid a.
  SensorReport invalid_report5 = {.name = "rgbc-light",
                                  .data = {Numeric(100L), Numeric(200L), Numeric(300L), "string"}};
  EXPECT_EQ(dut_->OnReport(invalid_report5), ZX_ERR_INVALID_ARGS);
}

}  // namespace goldfish::sensor
