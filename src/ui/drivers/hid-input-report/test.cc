// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <unistd.h>

#include <ddktl/protocol/hiddevice.h>
#include <hid/ambient-light.h>
#include <hid/boot.h>
#include <hid/paradise.h>
#include <hid/usages.h>
#include <zxtest/zxtest.h>

#include "input-report.h"
#include "src/ui/lib/hid-input-report/descriptors.h"

namespace hid_input_report_dev {

struct ProtocolDeviceOps {
  const zx_protocol_device_t* ops;
  void* ctx;
};

// Create our own Fake Ddk Bind class. We want to save the last device arguments that
// have been seen, so the test can get ahold of the instance device and test
// Reads and Writes on it.
class Binder : public fake_ddk::Bind {
 public:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status;

    if (args && args->ops) {
      if (args->ops->message) {
        if ((status = fidl_.SetMessageOp(args->ctx, args->ops->message)) < 0) {
          return status;
        }
      }
    }

    *out = fake_ddk::kFakeDevice;
    add_called_ = true;

    last_ops_.ctx = args->ctx;
    last_ops_.ops = args->ops;

    return ZX_OK;
  }

  ProtocolDeviceOps GetLastDeviceOps() { return last_ops_; }

 private:
  ProtocolDeviceOps last_ops_;
};

const uint8_t boot_mouse_desc[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Button)
    0x19, 0x01,  //     Usage Minimum (0x01)
    0x29, 0x03,  //     Usage Maximum (0x03)
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x95, 0x03,  //     Report Count (3)
    0x75, 0x01,  //     Report Size (1)
    0x81, 0x02,  //     Input (Data,Var,Abs,No Wrap,Linear,No Null Position)
    0x95, 0x01,  //     Report Count (1)
    0x75, 0x05,  //     Report Size (5)
    0x81, 0x03,  //     Input (Const,Var,Abs,No Wrap,Linear,No Null Position
    0x05, 0x01,  //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x02,  //     Report Count (2)
    0x81, 0x06,  //     Input (Data,Var,Rel,No Wrap,Linear,No Null Position)
    0xC0,        //   End Collection
    0xC0,        // End Collection
};

class FakeHidDevice : public ddk::HidDeviceProtocol<FakeHidDevice> {
 public:
  FakeHidDevice() : proto_({&hid_device_protocol_ops_, this}) {}

  zx_status_t HidDeviceRegisterListener(const hid_report_listener_protocol_t* listener) {
    listener_ = *listener;
    return ZX_OK;
  }

  void HidDeviceUnregisterListener() {}

  zx_status_t HidDeviceGetDescriptor(uint8_t* out_descriptor_list, size_t descriptor_count,
                                     size_t* out_descriptor_actual) {
    if (descriptor_count < report_desc_.size()) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(out_descriptor_list, report_desc_.data(), report_desc_.size());
    *out_descriptor_actual = report_desc_.size();
    return ZX_OK;
  }

  zx_status_t HidDeviceGetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                 uint8_t* out_report_list, size_t report_count,
                                 size_t* out_report_actual) {
    return ZX_OK;
  }

  zx_status_t HidDeviceSetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                 const uint8_t* report_list, size_t report_count) {
    return ZX_OK;
  }

  void SetReportDesc(std::vector<uint8_t> report_desc) { report_desc_ = report_desc; }
  void SetReport(std::vector<uint8_t> report) { report_ = report; }

  void SendReport() {
    listener_.ops->receive_report(listener_.ctx, report_.data(), report_.size());
  }

  hid_report_listener_protocol_t listener_;
  hid_device_protocol_t proto_;
  std::vector<uint8_t> report_desc_;
  std::vector<uint8_t> report_;
};

class HidDevTest : public zxtest::Test {
  void SetUp() override {
    client_ = ddk::HidDeviceProtocolClient(&fake_hid_.proto_);
    device_ = new InputReport(fake_ddk::kFakeParent, client_);
    // Each test is responsible for calling |device_->Bind()|.
  }

  void TearDown() override {
    device_->DdkAsyncRemove();
    EXPECT_TRUE(ddk_.Ok());

    // This should delete the object, which means this test should not leak.
    device_->DdkRelease();
  }

 protected:
  Binder ddk_;
  FakeHidDevice fake_hid_;
  InputReport* device_;
  ddk::HidDeviceProtocolClient client_;
};

TEST_F(HidDevTest, HidLifetimeTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  ASSERT_OK(device_->Bind());
}

TEST_F(HidDevTest, InstanceLifetimeTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  ASSERT_OK(device_->Bind());

  // Open an instance device.
  zx_device_t* open_dev;
  ASSERT_OK(device_->DdkOpen(&open_dev, 0));
  ProtocolDeviceOps dev_ops = ddk_.GetLastDeviceOps();
  // Close the instance device.
  dev_ops.ops->close(dev_ops.ctx, 0);
}

TEST_F(HidDevTest, GetReportDescTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  ASSERT_OK(device_->Bind());

  // Open an instance device.
  zx_device_t* open_dev;
  ASSERT_OK(device_->DdkOpen(&open_dev, 0));
  // Opening the device created an instance device to be created, and we can
  // get its arguments here.
  ProtocolDeviceOps dev_ops = ddk_.GetLastDeviceOps();

  auto sync_client = fuchsia_input_report::InputDevice::SyncClient(std::move(ddk_.FidlClient()));
  fuchsia_input_report::InputDevice::ResultOf::GetDescriptor result = sync_client.GetDescriptor();
  ASSERT_OK(result.status());

  auto& desc = result.Unwrap()->descriptor;
  ASSERT_TRUE(desc.has_mouse());
  ASSERT_TRUE(desc.mouse().has_input());
  fuchsia_input_report::MouseInputDescriptor& mouse = desc.mouse().input();

  ASSERT_TRUE(mouse.has_movement_x());
  ASSERT_EQ(-127, mouse.movement_x().range.min);
  ASSERT_EQ(127, mouse.movement_x().range.max);

  ASSERT_TRUE(mouse.has_movement_y());
  ASSERT_EQ(-127, mouse.movement_y().range.min);
  ASSERT_EQ(127, mouse.movement_y().range.max);

  // Close the instance device.
  dev_ops.ops->close(dev_ops.ctx, 0);
}

TEST_F(HidDevTest, GetReportTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  device_->Bind();

  // Open an instance device.
  zx_device_t* open_dev;
  ASSERT_OK(device_->DdkOpen(&open_dev, 0));
  // Opening the device created an instance device to be created, and we can
  // get its arguments here.
  ProtocolDeviceOps dev_ops = ddk_.GetLastDeviceOps();

  auto sync_client = fuchsia_input_report::InputDevice::SyncClient(std::move(ddk_.FidlClient()));

  // Spoof send a report.
  std::vector<uint8_t> sent_report = {0xFF, 0x50, 0x70};
  fake_hid_.SetReport(sent_report);
  fake_hid_.SendReport();

  // Get the report.
  fuchsia_input_report::InputDevice::ResultOf::GetReports result = sync_client.GetReports();
  ASSERT_OK(result.status());
  auto& reports = result.Unwrap()->reports;

  ASSERT_EQ(1, reports.count());

  auto& report = reports[0];
  ASSERT_TRUE(report.has_mouse());
  auto& mouse = report.mouse();

  ASSERT_TRUE(mouse.has_movement_x());
  ASSERT_EQ(0x50, mouse.movement_x());

  ASSERT_TRUE(mouse.has_movement_y());
  ASSERT_EQ(0x70, mouse.movement_y());

  ASSERT_TRUE(mouse.has_pressed_buttons());
  fidl::VectorView<uint8_t> pressed_buttons = mouse.pressed_buttons();
  for (size_t i = 0; i < pressed_buttons.count(); i++) {
    ASSERT_EQ(i + 1, pressed_buttons[i]);
  }

  // Close the instance device.
  dev_ops.ops->close(dev_ops.ctx, 0);
}

TEST_F(HidDevTest, SensorTest) {
  const uint8_t* sensor_desc_ptr;
  size_t sensor_desc_size = get_ambient_light_report_desc(&sensor_desc_ptr);
  std::vector<uint8_t> sensor_desc_vector(sensor_desc_ptr, sensor_desc_ptr + sensor_desc_size);
  fake_hid_.SetReportDesc(sensor_desc_vector);

  device_->Bind();

  // Open an instance device.
  zx_device_t* open_dev;
  ASSERT_OK(device_->DdkOpen(&open_dev, 0));
  // Opening the device created an instance device to be created, and we can
  // get its arguments here.
  ProtocolDeviceOps dev_ops = ddk_.GetLastDeviceOps();

  auto sync_client = fuchsia_input_report::InputDevice::SyncClient(std::move(ddk_.FidlClient()));

  // Get the report descriptor.
  fuchsia_input_report::InputDevice::ResultOf::GetDescriptor result = sync_client.GetDescriptor();
  ASSERT_OK(result.status());
  fuchsia_input_report::DeviceDescriptor& desc = result->descriptor;
  ASSERT_TRUE(desc.has_sensor());
  ASSERT_TRUE(desc.sensor().has_input());
  fuchsia_input_report::SensorInputDescriptor& sensor_desc = desc.sensor().input();
  ASSERT_TRUE(sensor_desc.has_values());
  ASSERT_EQ(4, sensor_desc.values().count());

  ASSERT_EQ(sensor_desc.values()[0].type, fuchsia_input_report::SensorType::LIGHT_ILLUMINANCE);
  ASSERT_EQ(sensor_desc.values()[0].axis.unit, fuchsia_input_report::Unit::LUX);

  ASSERT_EQ(sensor_desc.values()[1].type, fuchsia_input_report::SensorType::LIGHT_RED);
  ASSERT_EQ(sensor_desc.values()[1].axis.unit, fuchsia_input_report::Unit::LUX);

  ASSERT_EQ(sensor_desc.values()[2].type, fuchsia_input_report::SensorType::LIGHT_BLUE);
  ASSERT_EQ(sensor_desc.values()[2].axis.unit, fuchsia_input_report::Unit::LUX);

  ASSERT_EQ(sensor_desc.values()[3].type, fuchsia_input_report::SensorType::LIGHT_GREEN);
  ASSERT_EQ(sensor_desc.values()[3].axis.unit, fuchsia_input_report::Unit::LUX);

  // Create the report.
  ambient_light_input_rpt_t report_data = {};
  // Values are arbitrarily chosen.
  const int kIlluminanceTestVal = 10;
  const int kRedTestVal = 101;
  const int kBlueTestVal = 5;
  const int kGreenTestVal = 3;
  report_data.rpt_id = AMBIENT_LIGHT_RPT_ID_INPUT;
  report_data.illuminance = kIlluminanceTestVal;
  report_data.red = kRedTestVal;
  report_data.blue = kBlueTestVal;
  report_data.green = kGreenTestVal;

  std::vector<uint8_t> report_vector(
      reinterpret_cast<uint8_t*>(&report_data),
      reinterpret_cast<uint8_t*>(&report_data) + sizeof(report_data));
  fake_hid_.SetReport(report_vector);
  fake_hid_.SendReport();

  // Get the report.
  fuchsia_input_report::InputDevice::ResultOf::GetReports report_result = sync_client.GetReports();
  ASSERT_OK(result.status());

  fidl::VectorView<fuchsia_input_report::InputReport>& reports = report_result->reports;
  ASSERT_EQ(1, reports.count());

  ASSERT_TRUE(reports[0].has_sensor());
  fuchsia_input_report::SensorInputReport& sensor_report = reports[0].sensor();
  EXPECT_TRUE(sensor_report.has_values());
  EXPECT_EQ(4, sensor_report.values().count());

  // Check the report.
  // These will always match the ordering in the descriptor.
  constexpr uint32_t kLightUnitConversion = 100;
  EXPECT_EQ(kIlluminanceTestVal * kLightUnitConversion, sensor_report.values()[0]);
  EXPECT_EQ(kRedTestVal * kLightUnitConversion, sensor_report.values()[1]);
  EXPECT_EQ(kBlueTestVal * kLightUnitConversion, sensor_report.values()[2]);
  EXPECT_EQ(kGreenTestVal * kLightUnitConversion, sensor_report.values()[3]);

  // Close the instance device.
  dev_ops.ops->close(dev_ops.ctx, 0);
}

TEST_F(HidDevTest, GetTouchInputReportTest) {
  size_t desc_len;
  const uint8_t* report_desc = get_paradise_touch_report_desc(&desc_len);
  std::vector<uint8_t> desc(report_desc, report_desc + desc_len);
  fake_hid_.SetReportDesc(desc);

  device_->Bind();

  // Open an instance device.
  zx_device_t* open_dev;
  ASSERT_OK(device_->DdkOpen(&open_dev, 0));
  // Opening the device created an instance device to be created, and we can
  // get its arguments here.
  ProtocolDeviceOps dev_ops = ddk_.GetLastDeviceOps();

  auto sync_client = fuchsia_input_report::InputDevice::SyncClient(std::move(ddk_.FidlClient()));

  // Spoof send a report.
  paradise_touch_t touch_report = {};
  touch_report.rpt_id = PARADISE_RPT_ID_TOUCH;
  touch_report.contact_count = 1;
  touch_report.fingers[0].flags = 0xFF;
  touch_report.fingers[0].x = 100;
  touch_report.fingers[0].y = 200;
  touch_report.fingers[0].finger_id = 1;

  std::vector<uint8_t> sent_report =
      std::vector<uint8_t>(reinterpret_cast<uint8_t*>(&touch_report),
                           reinterpret_cast<uint8_t*>(&touch_report) + sizeof(touch_report));
  fake_hid_.SetReport(sent_report);
  fake_hid_.SendReport();

  // Get the report.
  fuchsia_input_report::InputDevice::ResultOf::GetReports result = sync_client.GetReports();
  ASSERT_OK(result.status());
  auto& reports = result.Unwrap()->reports;

  ASSERT_EQ(1, reports.count());

  auto& report = reports[0];
  auto& touch = report.touch();
  ASSERT_TRUE(touch.has_contacts());
  ASSERT_EQ(1, touch.contacts().count());
  auto& contact = touch.contacts()[0];

  ASSERT_TRUE(contact.has_position_x());
  ASSERT_EQ(2500, contact.position_x());

  ASSERT_TRUE(contact.has_position_y());
  ASSERT_EQ(5000, contact.position_y());

  // Close the instance device.
  dev_ops.ops->close(dev_ops.ctx, 0);
}

TEST_F(HidDevTest, KeyboardTest) {
  size_t keyboard_descriptor_size;
  const uint8_t* keyboard_descriptor = get_boot_kbd_report_desc(&keyboard_descriptor_size);
  std::vector<uint8_t> desc(keyboard_descriptor, keyboard_descriptor + keyboard_descriptor_size);

  fake_hid_.SetReportDesc(desc);
  device_->Bind();

  // Open an instance device.
  zx_device_t* open_dev;
  ASSERT_OK(device_->DdkOpen(&open_dev, 0));
  // Opening the device created an instance device to be created, and we can
  // get its arguments here.
  ProtocolDeviceOps dev_ops = ddk_.GetLastDeviceOps();

  auto sync_client = fuchsia_input_report::InputDevice::SyncClient(std::move(ddk_.FidlClient()));
  // Spoof send a report.
  hid_boot_kbd_report keyboard_report = {};
  keyboard_report.usage[0] = HID_USAGE_KEY_A;
  keyboard_report.usage[1] = HID_USAGE_KEY_UP;
  keyboard_report.usage[2] = HID_USAGE_KEY_B;

  std::vector<uint8_t> sent_report(
      reinterpret_cast<uint8_t*>(&keyboard_report),
      reinterpret_cast<uint8_t*>(&keyboard_report) + sizeof(keyboard_report));
  fake_hid_.SetReport(sent_report);
  fake_hid_.SendReport();

  // Get the report.
  fuchsia_input_report::InputDevice::ResultOf::GetReports result = sync_client.GetReports();
  ASSERT_OK(result.status());
  auto& reports = result->reports;

  ASSERT_EQ(1, reports.count());

  auto& report = reports[0];
  auto& keyboard = report.keyboard();
  ASSERT_TRUE(keyboard.has_pressed_keys());
  ASSERT_EQ(3, keyboard.pressed_keys().count());
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::A, keyboard.pressed_keys()[0]);
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::UP, keyboard.pressed_keys()[1]);
  EXPECT_EQ(llcpp::fuchsia::ui::input2::Key::B, keyboard.pressed_keys()[2]);

  // Close the instance device.
  dev_ops.ops->close(dev_ops.ctx, 0);
}
}  // namespace hid_input_report_dev
