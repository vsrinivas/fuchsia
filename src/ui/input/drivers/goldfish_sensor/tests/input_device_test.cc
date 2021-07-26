// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/input_device.h"

#include <fuchsia/hardware/goldfish/llcpp/fidl.h>
#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/zx/time.h>

#include <cmath>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/input/lib/input-report-reader/reader.h"

namespace goldfish::sensor {

class InputDeviceTest : public ::testing::Test {
 public:
  void SetUp() override {}
  void TearDown() override {}

 protected:
  fake_ddk::Bind ddk_;
  std::unique_ptr<async::TestLoop> loop_;
  std::unique_ptr<InputDevice> dut_;
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> fidl_client_;
};

class TestAccelerationInputDevice : public AccelerationInputDevice {
 public:
  explicit TestAccelerationInputDevice(async_dispatcher_t* dispatcher)
      : AccelerationInputDevice(fake_ddk::kFakeParent, dispatcher, nullptr) {}

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

class AccelerationInputDeviceTest : public InputDeviceTest {
 public:
  void SetUp() override {
    loop_ = std::make_unique<async::TestLoop>();
    dut_ = std::make_unique<TestAccelerationInputDevice>(loop_->dispatcher());
    ASSERT_EQ(dut_->DdkAdd("goldfish-sensor-accel"), ZX_OK);
    auto client_end =
        fidl::ClientEnd<fuchsia_input_report::InputDevice>(std::move(ddk_.FidlClient()));
    fidl_client_ = fidl::WireSyncClient<fuchsia_input_report::InputDevice>(std::move(client_end));
  }

  void TearDown() override {
    // After tests finishes, there might still be some pending events on the
    // loop that uses the InputReportsReader which could cause a use-after-free
    // error if we destroy the loop after destroying the device. Thus we destroy
    // the loop first. This is safe as long as we don't call
    // GetInputReportsReader() after resetting |loop_|.
    loop_.reset();
    dut_.reset();
  }

  TestAccelerationInputDevice* dut() const {
    return static_cast<TestAccelerationInputDevice*>(dut_.get());
  }
};

TEST_F(AccelerationInputDeviceTest, ReadInputReports) {
  auto endpoint_result = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  ASSERT_TRUE(endpoint_result.is_ok());
  auto [client_end, server_end] = std::move(endpoint_result.value());

  auto reader_result = fidl_client_.GetInputReportsReader(std::move(server_end));
  ASSERT_TRUE(reader_result.ok());
  auto client = fidl::Client<fuchsia_input_report::InputReportsReader>(std::move(client_end),
                                                                       loop_->dispatcher());

  // The FIDL callback runs on another thread by fake_ddk::FidlMessenger.
  // We will need to wait for the FIDL callback to finish before using |client|.
  while (!dut()->readers_created()) {
  }

  SensorReport rpt = {.name = "acceleration", .data = {Numeric(1.0), Numeric(2.0), Numeric(3.0)}};
  EXPECT_EQ(dut_->OnReport(rpt), ZX_OK);

  auto result = client->ReadInputReports(
      [](fidl::WireResponse<fuchsia_input_report::InputReportsReader::ReadInputReports>* response) {
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
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(loop_->RunUntilIdle());

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(AccelerationInputDeviceTest, Descriptor) {
  auto endpoint_result = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  ASSERT_TRUE(endpoint_result.is_ok());
  auto [client_end, server_end] = std::move(endpoint_result.value());

  auto descriptor_result = fidl_client_.GetDescriptor();
  ASSERT_TRUE(descriptor_result.ok());
  const auto& descriptor = descriptor_result->descriptor;

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

  EXPECT_EQ(values[0].axis.unit.type, fuchsia_input_report::wire::UnitType::kSiLinearAcceleration);
  EXPECT_EQ(values[1].axis.unit.type, fuchsia_input_report::wire::UnitType::kSiLinearAcceleration);
  EXPECT_EQ(values[2].axis.unit.type, fuchsia_input_report::wire::UnitType::kSiLinearAcceleration);

  EXPECT_EQ(values[0].axis.unit.exponent, -2);
  EXPECT_EQ(values[1].axis.unit.exponent, -2);
  EXPECT_EQ(values[2].axis.unit.exponent, -2);

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
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
  explicit TestGyroscopeInputDevice(async_dispatcher_t* dispatcher)
      : GyroscopeInputDevice(fake_ddk::kFakeParent, dispatcher, nullptr) {}

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

class GyroscopeInputDeviceTest : public InputDeviceTest {
 public:
  void SetUp() override {
    loop_ = std::make_unique<async::TestLoop>();
    dut_ = std::make_unique<TestGyroscopeInputDevice>(loop_->dispatcher());
    ASSERT_EQ(dut_->DdkAdd("goldfish-sensor-gyroscope"), ZX_OK);
    auto client_end =
        fidl::ClientEnd<fuchsia_input_report::InputDevice>(std::move(ddk_.FidlClient()));
    fidl_client_ = fidl::WireSyncClient<fuchsia_input_report::InputDevice>(std::move(client_end));
  }

  void TearDown() override {
    // After tests finishes, there might still be some pending events on the
    // loop that uses the InputReportsReader which could cause a use-after-free
    // error if we destroy the loop after destroying the device. Thus we destroy
    // the loop first. This is safe as long as we don't call
    // GetInputReportsReader() after resetting |loop_|.
    loop_.reset();
    dut_.reset();
  }

  TestGyroscopeInputDevice* dut() const {
    return static_cast<TestGyroscopeInputDevice*>(dut_.get());
  }
};

TEST_F(GyroscopeInputDeviceTest, ReadInputReports) {
  auto endpoint_result = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  ASSERT_TRUE(endpoint_result.is_ok());
  auto [client_end, server_end] = std::move(endpoint_result.value());

  auto reader_result = fidl_client_.GetInputReportsReader(std::move(server_end));
  ASSERT_TRUE(reader_result.ok());
  auto client = fidl::Client<fuchsia_input_report::InputReportsReader>(std::move(client_end),
                                                                       loop_->dispatcher());

  // The FIDL callback runs on another thread by fake_ddk::FidlMessenger.
  // We will need to wait for the FIDL callback to finish before using |client|.
  while (!dut()->readers_created()) {
  }

  SensorReport rpt = {.name = "gyroscope",
                      .data = {Numeric(M_PI), Numeric(2.0 * M_PI), Numeric(3.0 * M_PI)}};
  EXPECT_EQ(dut_->OnReport(rpt), ZX_OK);

  auto result = client->ReadInputReports(
      [](fidl::WireResponse<fuchsia_input_report::InputReportsReader::ReadInputReports>* response) {
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
  ASSERT_TRUE(result.ok());
  ASSERT_TRUE(loop_->RunUntilIdle());

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
}

TEST_F(GyroscopeInputDeviceTest, Descriptor) {
  auto endpoint_result = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
  ASSERT_TRUE(endpoint_result.is_ok());
  auto [client_end, server_end] = std::move(endpoint_result.value());

  auto descriptor_result = fidl_client_.GetDescriptor();
  ASSERT_TRUE(descriptor_result.ok());
  const auto& descriptor = descriptor_result->descriptor;

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

  dut_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());
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

}  // namespace goldfish::sensor
