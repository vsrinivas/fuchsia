// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/mcu/drivers/chromiumos-ec-core/motion.h"

#include <inttypes.h>

#include <memory>
#include <vector>

#include <chromiumos-platform-ec/ec_commands.h>
#include <hid-parser/parser.h>
#include <zxtest/zxtest.h>

#include "src/devices/mcu/drivers/chromiumos-ec-core/fake_device.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace chromiumos_ec_core::motion {
namespace {

SensorInfo SampleSensorInfo(enum motionsensor_type type) {
  SensorInfo info = {};
  info.valid = true;
  info.type = type;
  info.loc = MOTIONSENSE_LOC_BASE;
  info.max_sampling_freq = 10;
  info.min_sampling_freq = 10;
  info.fifo_max_event_count = 1;
  info.phys_min = -3;
  info.phys_max = 3;
  return info;
}

TEST(BuildHidDescriptor, SingleDescriptorParses) {
  // Generate a HID descriptor.
  fbl::Array<uint8_t> result;
  ASSERT_OK(BuildHidDescriptor(std::vector<SensorInfo>{SampleSensorInfo(MOTIONSENSE_TYPE_ACCEL)},
                               &result));

  // Ensure it parses again.
  hid::DeviceDescriptor* parsed_hid;
  ASSERT_EQ(hid::kParseOk, hid::ParseReportDescriptor(result.data(), result.size(), &parsed_hid));
  hid::FreeDeviceDescriptor(parsed_hid);
}

TEST(BuildHidDescriptor, AllSensors) {
  // Generate a HID descriptor of all supported sensors.
  auto sensors = std::vector<SensorInfo>{
      SampleSensorInfo(MOTIONSENSE_TYPE_ACCEL),
      SampleSensorInfo(MOTIONSENSE_TYPE_GYRO),
      SampleSensorInfo(MOTIONSENSE_TYPE_MAG),
      SampleSensorInfo(MOTIONSENSE_TYPE_LIGHT),
  };
  fbl::Array<uint8_t> result;
  ASSERT_OK(BuildHidDescriptor(sensors, &result));

  // Ensure it parses again.
  hid::DeviceDescriptor* parsed_hid;
  ASSERT_EQ(hid::kParseOk, hid::ParseReportDescriptor(result.data(), result.size(), &parsed_hid));
  ASSERT_EQ(parsed_hid->rep_count, sensors.size());
  hid::FreeDeviceDescriptor(parsed_hid);
}

// A Fake EmbeddedController with MotionSense support for 1 sensor.
class ChromiumosEcMotionTest : public ChromiumosEcTestBase {
  void SetUp() override {
    ChromiumosEcTestBase::SetUp();

    // Set up our EC.
    fake_ec_.SetFeatures({EC_FEATURE_MOTION_SENSE, EC_FEATURE_MOTION_SENSE_FIFO});
    fake_ec_.AddCommand(
        EC_CMD_MOTION_SENSE_CMD, 3, [this](auto* data, size_t data_size, auto& completer) {
          ASSERT_EQ(data_size, sizeof(ec_params_motion_sense));
          MotionsenseCommand(reinterpret_cast<const ec_params_motion_sense*>(data), completer);
        });

    fake_acpi_.SetInstallNotifyHandler(
        [](auto request, auto& completer) { completer.ReplySuccess(); });

    // Calls DdkInit on the cros-ec-core device.
    ASSERT_NO_FATAL_FAILURES(InitDevice());

    // Initialise the motion device.
    zx_device* motion_dev = device_->zxdev()->GetLatestChild();
    motion_dev->InitOp();
    ASSERT_OK(motion_dev->WaitUntilInitReplyCalled(zx::time::infinite()));
    motion_dev_ = motion_dev->GetDeviceContext<AcpiCrOsEcMotionDevice>();
  }
  void MotionsenseCommand(const ec_params_motion_sense* cmd,
                          FakeEcDevice::RunCommandCompleter::Sync& completer) {
    // Parse command.
    struct ec_response_motion_sense rsp = {};
    fidl::VectorView<uint8_t> response;
    switch (cmd->cmd) {
      case MOTIONSENSE_CMD_DUMP:
        // We only support one sensor.
        rsp.dump.sensor_count = 1;
        response = MakeVectorView(rsp.dump);
        break;

      case MOTIONSENSE_CMD_INFO:
        // Return information about our sensor.
        ZX_ASSERT(cmd->info_3.sensor_num == 0);
        rsp.info_3.type = MOTIONSENSE_TYPE_LIGHT;
        rsp.info_3.location = MOTIONSENSE_LOC_LID;
        rsp.info_3.min_frequency = 0;
        rsp.info_3.max_frequency = 100;
        rsp.info_3.fifo_max_event_count = 5;
        response = MakeVectorView(rsp.info_3);
        break;

      case MOTIONSENSE_CMD_FIFO_INT_ENABLE:
        // Enable/disable interrupts.
        interrupt_enabled_ = cmd->fifo_int_enable.enable != 0;
        rsp.fifo_int_enable.ret = 0;
        response = MakeVectorView(rsp.fifo_int_enable);
        break;

      case MOTIONSENSE_CMD_SENSOR_RANGE:
        // Return information about our sensor.
        ZX_ASSERT(cmd->sensor_range.sensor_num == 0);
        // We only support reads in this fake.
        ZX_ASSERT(cmd->sensor_range.data == EC_MOTION_SENSE_NO_VALUE);
        rsp.sensor_range.ret = 123;
        response = MakeVectorView(rsp.sensor_range);
        break;

      default:
        ZX_PANIC("Unsupported command: %d\n", cmd->cmd);
    }

    completer.ReplySuccess(fuchsia_hardware_google_ec::wire::EcStatus::kSuccess, response);
  }

 protected:
  bool interrupt_enabled_ = false;
  AcpiCrOsEcMotionDevice* motion_dev_;
};

TEST_F(ChromiumosEcMotionTest, Lifecycle) {
  // Ensure devices were probed correctly.
  uint8_t buffer[1024];
  size_t report_size;
  ASSERT_OK(motion_dev_->HidbusGetDescriptor(HID_DESCRIPTION_TYPE_REPORT,
                                             reinterpret_cast<uint8_t*>(&buffer), sizeof(buffer),
                                             &report_size));

  // Ensure the report parses, and contains 1 sensor (as simulated by the FakeMotionSenseEC).
  hid::DeviceDescriptor* parsed_hid;
  ASSERT_EQ(hid::kParseOk, hid::ParseReportDescriptor(buffer, report_size, &parsed_hid));
  ASSERT_EQ(parsed_hid->rep_count, 1);
  hid::FreeDeviceDescriptor(parsed_hid);
}

}  // namespace
}  // namespace chromiumos_ec_core::motion
