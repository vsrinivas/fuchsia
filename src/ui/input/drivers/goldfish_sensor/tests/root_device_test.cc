// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/input/drivers/goldfish_sensor/root_device.h"

#include <fuchsia/hardware/goldfish/pipe/cpp/banjo.h>
#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/ddk/driver.h>

#include <string>
#include <variant>

#include <gtest/gtest.h>

#include "src/devices/testing/goldfish/fake_pipe/fake_pipe.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/input/drivers/goldfish_sensor/input_device.h"

namespace goldfish::sensor {

namespace {

class FakeInputDevice : public InputDevice {
 public:
  static fit::result<InputDevice*, zx_status_t> Create(RootDevice* rootdevice,
                                                       async_dispatcher_t* dispatcher,
                                                       const std::string& name) {
    auto val = new FakeInputDevice(rootdevice, dispatcher, name);
    g_devices_[name] = val;
    return fit::ok(val);
  }

  FakeInputDevice(RootDevice* rootdevice, async_dispatcher_t* dispatcher, const std::string& name)
      : InputDevice(
            rootdevice->zxdev(), dispatcher,
            [rootdevice](InputDevice* dev) { rootdevice->input_devices()->RemoveDevice(dev); }),
        name_(name) {}

  ~FakeInputDevice() override { g_devices_.erase(name_); }

  zx_status_t OnReport(const SensorReport& rpt) override {
    std::vector<double> new_report;
    if (rpt.name != name_) {
      return ZX_ERR_INVALID_ARGS;
    }
    for (const auto& val : rpt.data) {
      if (std::holds_alternative<std::string>(val)) {
        return ZX_ERR_INVALID_ARGS;
      }
      new_report.push_back(std::get<Numeric>(val).Double());
    }
    report_ = std::move(new_report);
    report_id_++;
    return ZX_OK;
  }

  void GetInputReportsReader(GetInputReportsReaderRequestView request,
                             GetInputReportsReaderCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void GetDescriptor(GetDescriptorRequestView request,
                     GetDescriptorCompleter::Sync& completer) override {
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  static std::map<std::string, FakeInputDevice*> GetAllDevices() { return g_devices_; }
  static void EraseAllDevices() {
    for (const auto& kv : GetAllDevices()) {
      delete kv.second;
    }
  }

  std::vector<double> report() const { return report_; }
  uint32_t report_id() const { return report_id_.load(); }

 private:
  // For test purposes only.
  static inline std::map<std::string, FakeInputDevice*> g_devices_;

  std::atomic<uint32_t> report_id_ = 0;
  std::vector<double> report_;
  std::string name_;
};

fit::result<InputDevice*, zx_status_t> CreateFakeDevice1(RootDevice* rootdevice,
                                                         async_dispatcher_t* dispatcher) {
  return FakeInputDevice::Create(rootdevice, dispatcher, "fake1");
}

fit::result<InputDevice*, zx_status_t> CreateFakeDevice2(RootDevice* rootdevice,
                                                         async_dispatcher_t* dispatcher) {
  return FakeInputDevice::Create(rootdevice, dispatcher, "fake2");
}

const std::map<uint64_t, InputDeviceInfo> kFakeDevices = {
    {0x0001, {"fake1", CreateFakeDevice1}},
    {0x0002, {"fake2", CreateFakeDevice2}},
};

class TestRootDevice : public RootDevice {
 public:
  using RootDevice::OnReadSensor;
  using RootDevice::RootDevice;
};

class RootDeviceTest : public ::testing::Test {
 public:
  void SetUp() override {
    fake_parent_ = MockDevice::FakeRootParent();
    fake_parent_->AddProtocol(ZX_PROTOCOL_GOLDFISH_PIPE, fake_pipe_.proto()->ops,
                              fake_pipe_.proto()->ctx);

    auto device = std::make_unique<TestRootDevice>(fake_parent_.get());
    ASSERT_EQ(device->Bind(), ZX_OK);
    // dut_ will be deleted by MockDevice when test ends.
    dut_ = device.release();
    ASSERT_EQ(fake_parent_->child_count(), 1u);
  }

  void TearDown() override {
    FakeInputDevice::EraseAllDevices();

    if (dut_) {
      device_async_remove(dut_->zxdev());
      mock_ddk::ReleaseFlaggedDevices(fake_parent_.get());
    }
  }

 protected:
  std::shared_ptr<MockDevice> fake_parent_;
  TestRootDevice* dut_;
  testing::FakePipe fake_pipe_;
};

TEST_F(RootDeviceTest, SetupDevices) {
  bool list_sensors_called = false;
  fake_pipe_.SetOnCmdWriteCallback(
      [pipe = &this->fake_pipe_, &list_sensors_called](const std::vector<uint8_t>& cmd) {
        const char* kCmdExpected = "000clist-sensors";
        if (memcmp(cmd.data(), kCmdExpected, strlen(kCmdExpected)) == 0) {
          list_sensors_called = true;
          const char* kFrameLength = "0004";
          const char* kFrameContents = "0001";
          pipe->EnqueueBytesToRead(
              std::vector<uint8_t>(kFrameLength, kFrameLength + strlen(kFrameLength)));
          pipe->EnqueueBytesToRead(
              std::vector<uint8_t>(kFrameContents, kFrameContents + strlen(kFrameContents)));
        }
      });

  ASSERT_EQ(dut_->Setup(kFakeDevices), ZX_OK);
  EXPECT_EQ(FakeInputDevice::GetAllDevices().size(), 1u);
  EXPECT_TRUE(list_sensors_called);

  // Only fake1 is set.
  const char* kSetFake1 = "000bset:fake1:1";
  EXPECT_EQ(memcmp(fake_pipe_.io_buffer_contents().back().data(), kSetFake1, strlen(kSetFake1)), 0);
}

TEST_F(RootDeviceTest, SetupMultipleDevices) {
  bool list_sensors_called = false;
  fake_pipe_.SetOnCmdWriteCallback(
      [pipe = &this->fake_pipe_, &list_sensors_called](const std::vector<uint8_t>& cmd) {
        const char* kCmdExpected = "000clist-sensors";
        if (memcmp(cmd.data(), kCmdExpected, strlen(kCmdExpected)) == 0) {
          list_sensors_called = true;
          const char* kFrameLength = "0004";
          const char* kFrameContents = "0003";
          pipe->EnqueueBytesToRead(
              std::vector<uint8_t>(kFrameLength, kFrameLength + strlen(kFrameLength)));
          pipe->EnqueueBytesToRead(
              std::vector<uint8_t>(kFrameContents, kFrameContents + strlen(kFrameContents)));
        }
      });

  ASSERT_EQ(dut_->Setup(kFakeDevices), ZX_OK);
  EXPECT_EQ(FakeInputDevice::GetAllDevices().size(), 2u);
  EXPECT_TRUE(list_sensors_called);

  // Both fake1 and fake2 are set.
  const char* kSetFake1 = "000bset:fake1:1";
  const char* kSetFake2 = "000bset:fake2:1";
  EXPECT_EQ(memcmp(fake_pipe_.io_buffer_contents().rbegin()->data(), kSetFake2, strlen(kSetFake2)),
            0);
  EXPECT_EQ(
      memcmp((++fake_pipe_.io_buffer_contents().rbegin())->data(), kSetFake1, strlen(kSetFake1)),
      0);
}

TEST_F(RootDeviceTest, DispatchSensorReports) {
  // Set list-sensors mask to 0x03, enabling both fake1 and fake2 devices.
  bool list_sensors_called = false;
  fake_pipe_.SetOnCmdWriteCallback(
      [pipe = &this->fake_pipe_, &list_sensors_called](const std::vector<uint8_t>& cmd) {
        const char* kCmdExpected = "000clist-sensors";
        if (memcmp(cmd.data(), kCmdExpected, strlen(kCmdExpected)) == 0) {
          list_sensors_called = true;
          const char* kFrameLength = "0004";
          const char* kFrameContents = "0003";
          pipe->EnqueueBytesToRead(
              std::vector<uint8_t>(kFrameLength, kFrameLength + strlen(kFrameLength)));
          pipe->EnqueueBytesToRead(
              std::vector<uint8_t>(kFrameContents, kFrameContents + strlen(kFrameContents)));
        }
      });

  ASSERT_EQ(dut_->Setup(kFakeDevices), ZX_OK);
  EXPECT_EQ(FakeInputDevice::GetAllDevices().size(), 2u);
  EXPECT_TRUE(list_sensors_called);

  auto fake1 = FakeInputDevice::GetAllDevices().at("fake1");
  auto fake2 = FakeInputDevice::GetAllDevices().at("fake2");
  auto fake1_report_id = fake1->report_id();
  auto fake2_report_id = fake2->report_id();

  const char* kFake1Report = "fake1:0.1:0.2";
  PipeIo::ReadResult read_result =
      fit::ok(std::vector<uint8_t>(kFake1Report, kFake1Report + strlen(kFake1Report)));
  dut_->OnReadSensor(std::move(read_result));

  EXPECT_EQ(fake1->report_id(), fake1_report_id + 1);
  EXPECT_EQ(fake2->report_id(), fake2_report_id);
  EXPECT_EQ(fake1->report().size(), 2u);
  EXPECT_EQ(fake1->report()[0], 0.1);
  EXPECT_EQ(fake1->report()[1], 0.2);

  const char* kFake2Report = "fake2:0:0.2:0.3";
  read_result = fit::ok(std::vector<uint8_t>(kFake2Report, kFake2Report + strlen(kFake2Report)));
  dut_->OnReadSensor(std::move(read_result));

  EXPECT_EQ(fake1->report_id(), fake1_report_id + 1);
  EXPECT_EQ(fake2->report_id(), fake2_report_id + 1);
  EXPECT_EQ(fake2->report().size(), 3u);
  EXPECT_EQ(fake2->report()[0], 0);
  EXPECT_EQ(fake2->report()[1], 0.2);
  EXPECT_EQ(fake2->report()[2], 0.3);

  const char* kFake3Report = "fake3:1:2:3:4";
  read_result = fit::ok(std::vector<uint8_t>(kFake3Report, kFake3Report + strlen(kFake3Report)));
  dut_->OnReadSensor(std::move(read_result));

  EXPECT_EQ(fake1->report_id(), fake1_report_id + 1);
  EXPECT_EQ(fake2->report_id(), fake2_report_id + 1);
}

}  // namespace

}  // namespace goldfish::sensor
