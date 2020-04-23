// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "motion.h"

#include <inttypes.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <memory>
#include <vector>

#include <hid-parser/parser.h>
#include <zxtest/zxtest.h>

#include "dev.h"

namespace cros_ec {
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
class FakeMotionSenseEC : public EmbeddedController {
  zx_status_t IssueCommand(uint16_t command, uint8_t command_version, const void* input,
                           size_t input_size, void* result, size_t result_buff_size,
                           size_t* actual) override {
    // Ensure command is correct for MotionSense.
    if (command != EC_CMD_MOTION_SENSE_CMD) {
      return ZX_ERR_NOT_SUPPORTED;
    }
    ZX_ASSERT(input_size == sizeof(ec_params_motion_sense));
    struct ec_params_motion_sense cmd;
    memcpy(&cmd, input, input_size);

    // Parse command.
    struct ec_response_motion_sense rsp = {};
    size_t response_size;
    switch (cmd.cmd) {
      case MOTIONSENSE_CMD_DUMP:
        // We only support one sensor.
        rsp.dump.sensor_count = 1;
        response_size = sizeof(rsp.dump);
        break;

      case MOTIONSENSE_CMD_INFO:
        // Return information about our sensor.
        ZX_ASSERT(cmd.info_3.sensor_num == 0);
        rsp.info_3.type = MOTIONSENSE_TYPE_LIGHT;
        rsp.info_3.location = MOTIONSENSE_LOC_LID;
        rsp.info_3.min_frequency = 0;
        rsp.info_3.max_frequency = 100;
        rsp.info_3.fifo_max_event_count = 5;
        response_size = sizeof(rsp.info_3);
        break;

      case MOTIONSENSE_CMD_FIFO_INT_ENABLE:
        // Enable/disable interrupts.
        interrupt_enabled_ = cmd.fifo_int_enable.enable != 0;
        rsp.fifo_int_enable.ret = 0;
        response_size = sizeof(rsp.fifo_int_enable);
        break;

      case MOTIONSENSE_CMD_SENSOR_RANGE:
        // Return information about our sensor.
        ZX_ASSERT(cmd.sensor_range.sensor_num == 0);
        // We only support reads in this fake.
        ZX_ASSERT(cmd.sensor_range.data == EC_MOTION_SENSE_NO_VALUE);
        rsp.sensor_range.ret = 123;
        response_size = sizeof(rsp.sensor_range);
        break;

      default:
        ZX_PANIC("Unsupported command: %d\n", cmd.cmd);
    }

    // Copy response.
    memcpy(result, &rsp, response_size);
    *actual = response_size;
    return ZX_OK;
  }

  bool SupportsFeature(enum ec_feature_code feature) const override {
    return feature == EC_FEATURE_MOTION_SENSE || feature == EC_FEATURE_MOTION_SENSE_FIFO;
  }

 private:
  bool interrupt_enabled_ = false;
};

TEST(MotionSense, Lifecycle) {
  fake_ddk::Bind ddk;

  // Create the device.
  auto ec = fbl::MakeRefCounted<FakeMotionSenseEC>();
  AcpiCrOsEcMotionDevice* device;
  ASSERT_OK(
      AcpiCrOsEcMotionDevice::Bind(fake_ddk::kFakeParent, ec, CreateNoOpAcpiHandle(), &device));

  // Ensure devices were probed correctly.
  uint8_t buffer[1024];
  size_t report_size;
  ASSERT_OK(device->HidbusGetDescriptor(HID_DESCRIPTION_TYPE_REPORT, &buffer, sizeof(buffer),
                                        &report_size));

  // Ensure the report parses, and contains 1 sensor (as simulated by the FakeMotionSenseEC).
  hid::DeviceDescriptor* parsed_hid;
  ASSERT_EQ(hid::kParseOk, hid::ParseReportDescriptor(buffer, report_size, &parsed_hid));
  ASSERT_EQ(parsed_hid->rep_count, 1);
  hid::FreeDeviceDescriptor(parsed_hid);

  // Remove and destroy the device.
  device->DdkAsyncRemove();
  device->DdkRelease();

  // Ensure everything was ok.
  EXPECT_TRUE(ddk.Ok());
}

}  // namespace
}  // namespace cros_ec
