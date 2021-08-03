// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/hiddevice/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/inspect/testing/cpp/zxtest/inspect.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <ddk/metadata/buttons.h>
#include <hid/ambient-light.h>
#include <hid/boot.h>
#include <hid/buttons.h>
#include <hid/gt92xx.h>
#include <hid/paradise.h>
#include <hid/usages.h>
#include <zxtest/zxtest.h>

#include "input-report.h"

namespace hid_input_report_dev {
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

class SaveInspectVmoBind : public fake_ddk::Bind {
 public:
  zx::vmo TakeInspectVmo() { return std::move(inspect_vmo_); }

 protected:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    if (args) {
      inspect_vmo_.reset(args->inspect_vmo);
      args->inspect_vmo = ZX_HANDLE_INVALID;
    }
    return fake_ddk::Bind::DeviceAdd(drv, parent, args, out);
  }

 private:
  zx::vmo inspect_vmo_;
};

class FakeHidDevice : public ddk::HidDeviceProtocol<FakeHidDevice> {
 public:
  FakeHidDevice() : proto_({&hid_device_protocol_ops_, this}) {}

  zx_status_t HidDeviceRegisterListener(const hid_report_listener_protocol_t* listener) {
    listener_ = *listener;
    return ZX_OK;
  }

  void HidDeviceUnregisterListener() { listener_.reset(); }

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
    // If the client is Getting a report with a specific ID, check that it matches
    // our saved report.
    if ((rpt_id != 0) && (report_.size() > 0)) {
      if (rpt_id != report_[0]) {
        return ZX_ERR_WRONG_TYPE;
      }
    }

    if (report_count < report_.size()) {
      return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(out_report_list, report_.data(), report_.size());
    *out_report_actual = report_.size();

    return ZX_OK;
  }

  void HidDeviceGetHidDeviceInfo(hid_device_info_t* out_info) {
    out_info->vendor_id = 0xabc;
    out_info->product_id = 123;
    out_info->version = 5;
  }

  zx_status_t HidDeviceSetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                 const uint8_t* report_list, size_t report_count) {
    report_ = std::vector<uint8_t>(report_list, report_list + report_count);
    return ZX_OK;
  }

  void SetReportDesc(std::vector<uint8_t> report_desc) { report_desc_ = report_desc; }

  void SendReport(const std::vector<uint8_t>& report, zx_time_t timestamp = ZX_TIME_INFINITE) {
    if (timestamp == ZX_TIME_INFINITE) {
      timestamp = zx_clock_get_monotonic();
    }
    if (listener_.has_value()) {
      listener_->ops->receive_report(listener_->ctx, report.data(), report.size(), timestamp);
    }
  }

  std::optional<hid_report_listener_protocol_t> listener_;
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
  fidl::WireSyncClient<fuchsia_input_report::InputDevice> GetSyncClient() {
    return fidl::BindSyncClient(ddk_.FidlClient<fuchsia_input_report::InputDevice>());
  }

  SaveInspectVmoBind ddk_;
  FakeHidDevice fake_hid_;
  InputReport* device_;
  ddk::HidDeviceProtocolClient client_;
};

class FakeDdkBindFailure : public fake_ddk::Bind {
 protected:
  virtual zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                zx_device_t** out) override {
    return ZX_ERR_INTERNAL;
  }
};

TEST_F(HidDevTest, HidLifetimeTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  ASSERT_OK(device_->Bind());
}

TEST(HidDevTest, InputReportUnregisterTest) {
  fake_ddk::Bind ddk_;
  FakeHidDevice fake_hid_;
  InputReport* device_;
  ddk::HidDeviceProtocolClient client_;

  client_ = ddk::HidDeviceProtocolClient(&fake_hid_.proto_);
  device_ = new InputReport(fake_ddk::kFakeParent, client_);

  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  ASSERT_OK(device_->Bind());

  device_->DdkAsyncRemove();
  EXPECT_TRUE(ddk_.Ok());

  // This should delete the object, which means this test should not leak.
  device_->DdkRelease();

  // Make sure that the InputReport class has unregistered from the HID device.
  ASSERT_FALSE(fake_hid_.listener_);
}

TEST(HidDevTest, InputReportUnregisterTestBindFailed) {
  FakeDdkBindFailure ddk;
  FakeHidDevice fake_hid;
  ddk::HidDeviceProtocolClient client;

  client = ddk::HidDeviceProtocolClient(&fake_hid.proto_);
  auto device = std::make_unique<InputReport>(fake_ddk::kFakeParent, client);

  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid.SetReportDesc(boot_mouse);

  ASSERT_EQ(device->Bind(), ZX_ERR_INTERNAL);

  // Make sure that the InputReport class is not registered to the HID device.
  ASSERT_FALSE(fake_hid.listener_);
}

TEST_F(HidDevTest, GetReportDescTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  ASSERT_OK(device_->Bind());

  auto sync_client = GetSyncClient();
  fidl::WireResult<fuchsia_input_report::InputDevice::GetDescriptor> result =
      sync_client.GetDescriptor();
  ASSERT_OK(result.status());

  auto& desc = result.Unwrap()->descriptor;
  ASSERT_TRUE(desc.has_mouse());
  ASSERT_TRUE(desc.mouse().has_input());
  fuchsia_input_report::wire::MouseInputDescriptor& mouse = desc.mouse().input();

  ASSERT_TRUE(mouse.has_movement_x());
  ASSERT_EQ(-127, mouse.movement_x().range.min);
  ASSERT_EQ(127, mouse.movement_x().range.max);

  ASSERT_TRUE(mouse.has_movement_y());
  ASSERT_EQ(-127, mouse.movement_y().range.min);
  ASSERT_EQ(127, mouse.movement_y().range.max);
}

TEST_F(HidDevTest, ReportDescInfoTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  ASSERT_OK(device_->Bind());

  auto sync_client = GetSyncClient();
  fidl::WireResult<fuchsia_input_report::InputDevice::GetDescriptor> result =
      sync_client.GetDescriptor();
  ASSERT_OK(result.status());

  hid_device_info_t info;
  fake_hid_.HidDeviceGetHidDeviceInfo(&info);

  auto& desc = result->descriptor;
  ASSERT_TRUE(desc.has_device_info());
  ASSERT_EQ(desc.device_info().vendor_id, info.vendor_id);
  ASSERT_EQ(desc.device_info().product_id, info.product_id);
  ASSERT_EQ(desc.device_info().version, info.version);
}

TEST_F(HidDevTest, ReadInputReportsTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  device_->Bind();

  auto sync_client = GetSyncClient();

  // Get an InputReportsReader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_OK(endpoints.status_value());

    auto result = sync_client.GetInputReportsReader(std::move(endpoints->server));
    ASSERT_OK(result.status());
    reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(
        std::move(endpoints->client));
    ASSERT_OK(device_->WaitForNextReader(zx::duration::infinite()));
  }

  // Spoof send a report.
  std::vector<uint8_t> sent_report = {0xFF, 0x50, 0x70};
  fake_hid_.SendReport(sent_report);

  // Get the report.
  auto result = reader.ReadInputReports();
  ASSERT_OK(result.status());
  ASSERT_FALSE(result->result.is_err());
  auto& reports = result->result.response().reports;

  ASSERT_EQ(1, reports.count());

  auto& report = reports[0];
  ASSERT_TRUE(report.has_event_time());
  ASSERT_TRUE(report.has_mouse());
  auto& mouse = report.mouse();

  ASSERT_TRUE(mouse.has_movement_x());
  ASSERT_EQ(0x50, mouse.movement_x());

  ASSERT_TRUE(mouse.has_movement_y());
  ASSERT_EQ(0x70, mouse.movement_y());

  ASSERT_TRUE(mouse.has_pressed_buttons());
  const fidl::VectorView<uint8_t>& pressed_buttons = mouse.pressed_buttons();
  for (size_t i = 0; i < pressed_buttons.count(); i++) {
    ASSERT_EQ(i + 1, pressed_buttons[i]);
  }
}

TEST_F(HidDevTest, ReadInputReportsHangingGetTest) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  device_->Bind();

  auto sync_client = GetSyncClient();

  // Get an InputReportsReader.

  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_OK(endpoints.status_value());

    auto result = sync_client.GetInputReportsReader(std::move(endpoints->server));
    ASSERT_OK(result.status());
    reader.Bind(std::move(endpoints->client), loop.dispatcher());
    ASSERT_OK(device_->WaitForNextReader(zx::duration::infinite()));
  }

  // Read the report. This will hang until a report is sent.
  auto status = reader->ReadInputReports(
      [&](fidl::WireResponse<fuchsia_input_report::InputReportsReader::ReadInputReports>*
              response) {
        ASSERT_FALSE(response->result.is_err());
        auto& reports = response->result.response().reports;
        ASSERT_EQ(1, reports.count());

        auto& report = reports[0];
        ASSERT_TRUE(report.has_event_time());
        ASSERT_TRUE(report.has_mouse());
        auto& mouse = report.mouse();

        ASSERT_TRUE(mouse.has_movement_x());
        ASSERT_EQ(0x50, mouse.movement_x());

        ASSERT_TRUE(mouse.has_movement_y());
        ASSERT_EQ(0x70, mouse.movement_y());
        loop.Quit();
      });
  ASSERT_OK(status.status());
  loop.RunUntilIdle();

  // Send the report.
  std::vector<uint8_t> sent_report = {0xFF, 0x50, 0x70};
  fake_hid_.SendReport(sent_report);

  loop.Run();
}

TEST_F(HidDevTest, CloseReaderWithOutstandingRead) {
  std::vector<uint8_t> boot_mouse(boot_mouse_desc, boot_mouse_desc + sizeof(boot_mouse_desc));
  fake_hid_.SetReportDesc(boot_mouse);

  device_->Bind();

  auto sync_client = GetSyncClient();

  // Get an InputReportsReader.

  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_OK(endpoints.status_value());

    auto result = sync_client.GetInputReportsReader(std::move(endpoints->server));
    ASSERT_OK(result.status());
    reader.Bind(std::move(endpoints->client), loop.dispatcher());
    ASSERT_OK(device_->WaitForNextReader(zx::duration::infinite()));
  }

  // Queue a report.
  auto status = reader->ReadInputReports(
      [&](fidl::WireResponse<fuchsia_input_report::InputReportsReader::ReadInputReports>*
              response) { ASSERT_TRUE(response->result.is_err()); });
  ASSERT_OK(status.status());
  loop.RunUntilIdle();

  // Unbind the reader now that the report is waiting.
  reader = {};
}

TEST_F(HidDevTest, SensorTest) {
  const uint8_t* sensor_desc_ptr;
  size_t sensor_desc_size = get_ambient_light_report_desc(&sensor_desc_ptr);
  std::vector<uint8_t> sensor_desc_vector(sensor_desc_ptr, sensor_desc_ptr + sensor_desc_size);
  fake_hid_.SetReportDesc(sensor_desc_vector);

  device_->Bind();

  auto sync_client = GetSyncClient();

  // Get the report descriptor.
  fidl::WireResult<fuchsia_input_report::InputDevice::GetDescriptor> result =
      sync_client.GetDescriptor();
  ASSERT_OK(result.status());
  fuchsia_input_report::wire::DeviceDescriptor& desc = result->descriptor;
  ASSERT_TRUE(desc.has_sensor());
  ASSERT_TRUE(desc.sensor().has_input());
  fuchsia_input_report::wire::SensorInputDescriptor& sensor_desc = desc.sensor().input();
  ASSERT_TRUE(sensor_desc.has_values());
  ASSERT_EQ(4, sensor_desc.values().count());

  ASSERT_EQ(sensor_desc.values()[0].type,
            fuchsia_input_report::wire::SensorType::kLightIlluminance);
  ASSERT_EQ(sensor_desc.values()[0].axis.unit.type, fuchsia_input_report::wire::UnitType::kNone);

  ASSERT_EQ(sensor_desc.values()[1].type, fuchsia_input_report::wire::SensorType::kLightRed);
  ASSERT_EQ(sensor_desc.values()[1].axis.unit.type, fuchsia_input_report::wire::UnitType::kNone);

  ASSERT_EQ(sensor_desc.values()[2].type, fuchsia_input_report::wire::SensorType::kLightBlue);
  ASSERT_EQ(sensor_desc.values()[2].axis.unit.type, fuchsia_input_report::wire::UnitType::kNone);

  ASSERT_EQ(sensor_desc.values()[3].type, fuchsia_input_report::wire::SensorType::kLightGreen);
  ASSERT_EQ(sensor_desc.values()[3].axis.unit.type, fuchsia_input_report::wire::UnitType::kNone);

  // Get an InputReportsReader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_OK(endpoints.status_value());

    auto result = sync_client.GetInputReportsReader(std::move(endpoints->server));
    ASSERT_OK(result.status());
    reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(
        std::move(endpoints->client));
    ASSERT_OK(device_->WaitForNextReader(zx::duration::infinite()));
  }

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
  fake_hid_.SendReport(report_vector);

  // Get the report.
  auto report_result = reader.ReadInputReports();
  ASSERT_OK(report_result.status());

  const fidl::VectorView<fuchsia_input_report::wire::InputReport>& reports =
      report_result->result.response().reports;
  ASSERT_EQ(1, reports.count());

  ASSERT_TRUE(reports[0].has_sensor());
  const fuchsia_input_report::wire::SensorInputReport& sensor_report = reports[0].sensor();
  EXPECT_TRUE(sensor_report.has_values());
  EXPECT_EQ(4, sensor_report.values().count());

  // Check the report.
  // These will always match the ordering in the descriptor.
  EXPECT_EQ(kIlluminanceTestVal, sensor_report.values()[0]);
  EXPECT_EQ(kRedTestVal, sensor_report.values()[1]);
  EXPECT_EQ(kBlueTestVal, sensor_report.values()[2]);
  EXPECT_EQ(kGreenTestVal, sensor_report.values()[3]);
}

TEST_F(HidDevTest, GetTouchInputReportTest) {
  size_t desc_len;
  const uint8_t* report_desc = get_paradise_touch_report_desc(&desc_len);
  std::vector<uint8_t> desc(report_desc, report_desc + desc_len);
  fake_hid_.SetReportDesc(desc);

  device_->Bind();

  auto sync_client = GetSyncClient();

  // Get an InputReportsReader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_OK(endpoints.status_value());

    auto result = sync_client.GetInputReportsReader(std::move(endpoints->server));
    ASSERT_OK(result.status());
    reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(
        std::move(endpoints->client));
    ASSERT_OK(device_->WaitForNextReader(zx::duration::infinite()));
  }

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
  fake_hid_.SendReport(sent_report);

  // Get the report.
  auto report_result = reader.ReadInputReports();
  ASSERT_OK(report_result.status());

  const fidl::VectorView<fuchsia_input_report::wire::InputReport>& reports =
      report_result->result.response().reports;
  ASSERT_EQ(1, reports.count());

  const auto& report = reports[0];
  const auto& touch = report.touch();
  ASSERT_TRUE(touch.has_contacts());
  ASSERT_EQ(1, touch.contacts().count());
  const auto& contact = touch.contacts()[0];

  ASSERT_TRUE(contact.has_position_x());
  ASSERT_EQ(2500, contact.position_x());

  ASSERT_TRUE(contact.has_position_y());
  ASSERT_EQ(5000, contact.position_y());
}

TEST_F(HidDevTest, GetTouchPadDescTest) {
  size_t desc_len;
  const uint8_t* report_desc = get_paradise_touchpad_v1_report_desc(&desc_len);
  std::vector<uint8_t> desc(report_desc, report_desc + desc_len);
  fake_hid_.SetReportDesc(desc);

  device_->Bind();

  auto sync_client = GetSyncClient();
  fidl::WireResult<fuchsia_input_report::InputDevice::GetDescriptor> result =
      sync_client.GetDescriptor();
  ASSERT_OK(result.status());
  ASSERT_TRUE(result->descriptor.has_touch());
  ASSERT_TRUE(result->descriptor.touch().has_input());
  fuchsia_input_report::wire::TouchInputDescriptor& touch = result->descriptor.touch().input();

  ASSERT_EQ(fuchsia_input_report::wire::TouchType::kTouchpad, touch.touch_type());
}

TEST_F(HidDevTest, KeyboardTest) {
  size_t keyboard_descriptor_size;
  const uint8_t* keyboard_descriptor = get_boot_kbd_report_desc(&keyboard_descriptor_size);
  std::vector<uint8_t> desc(keyboard_descriptor, keyboard_descriptor + keyboard_descriptor_size);

  fake_hid_.SetReportDesc(desc);
  device_->Bind();

  auto sync_client = GetSyncClient();

  // Get an InputReportsReader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_OK(endpoints.status_value());

    auto result = sync_client.GetInputReportsReader(std::move(endpoints->server));
    ASSERT_OK(result.status());
    reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(
        std::move(endpoints->client));
    ASSERT_OK(device_->WaitForNextReader(zx::duration::infinite()));
  }

  // Spoof send a report.
  hid_boot_kbd_report keyboard_report = {};
  keyboard_report.usage[0] = HID_USAGE_KEY_A;
  keyboard_report.usage[1] = HID_USAGE_KEY_UP;
  keyboard_report.usage[2] = HID_USAGE_KEY_B;

  std::vector<uint8_t> sent_report(
      reinterpret_cast<uint8_t*>(&keyboard_report),
      reinterpret_cast<uint8_t*>(&keyboard_report) + sizeof(keyboard_report));
  fake_hid_.SendReport(sent_report);

  // Get the report.
  auto report_result = reader.ReadInputReports();
  ASSERT_OK(report_result.status());

  const fidl::VectorView<fuchsia_input_report::wire::InputReport>& reports =
      report_result->result.response().reports;
  ASSERT_EQ(1, reports.count());

  const auto& report = reports[0];
  const auto& keyboard = report.keyboard();
  ASSERT_EQ(3, keyboard.pressed_keys3().count());
  EXPECT_EQ(fuchsia_input::wire::Key::kA, keyboard.pressed_keys3()[0]);
  EXPECT_EQ(fuchsia_input::wire::Key::kUp, keyboard.pressed_keys3()[1]);
  EXPECT_EQ(fuchsia_input::wire::Key::kB, keyboard.pressed_keys3()[2]);
}

TEST_F(HidDevTest, KeyboardOutputReportTest) {
  size_t keyboard_descriptor_size;
  const uint8_t* keyboard_descriptor = get_boot_kbd_report_desc(&keyboard_descriptor_size);
  std::vector<uint8_t> desc(keyboard_descriptor, keyboard_descriptor + keyboard_descriptor_size);
  fake_hid_.SetReportDesc(desc);
  device_->Bind();

  auto sync_client = GetSyncClient();
  // Make an output report.
  fidl::Arena allocator;
  fidl::VectorView<fuchsia_input_report::wire::LedType> led_view(allocator, 2);
  led_view[0] = fuchsia_input_report::wire::LedType::kNumLock;
  led_view[1] = fuchsia_input_report::wire::LedType::kScrollLock;
  fuchsia_input_report::wire::KeyboardOutputReport fidl_keyboard(allocator);
  fidl_keyboard.set_enabled_leds(allocator, std::move(led_view));

  fuchsia_input_report::wire::OutputReport output_report(allocator);
  output_report.set_keyboard(allocator, std::move(fidl_keyboard));
  // Send the report.
  fidl::WireResult<fuchsia_input_report::InputDevice::SendOutputReport> response =
      sync_client.SendOutputReport(std::move(output_report));
  ASSERT_OK(response.status());
  ASSERT_FALSE(response->result.is_err());
  // Get and check the hid output report.
  uint8_t report;
  size_t out_size;
  ASSERT_OK(
      fake_hid_.HidDeviceGetReport(HID_REPORT_TYPE_OUTPUT, 0, &report, sizeof(report), &out_size));
  ASSERT_EQ(out_size, sizeof(report));
  ASSERT_EQ(report, 0b101);
}

TEST_F(HidDevTest, ConsumerControlTest) {
  {
    const uint8_t* descriptor;
    size_t descriptor_size = get_buttons_report_desc(&descriptor);
    std::vector<uint8_t> desc_vector(descriptor, descriptor + descriptor_size);
    fake_hid_.SetReportDesc(desc_vector);
  }

  // Create the initial report that will be queried on DdkOpen().
  {
    struct buttons_input_rpt report = {};
    report.rpt_id = BUTTONS_RPT_ID_INPUT;
    std::vector<uint8_t> report_vector(reinterpret_cast<uint8_t*>(&report),
                                       reinterpret_cast<uint8_t*>(&report) + sizeof(report));
    fake_hid_.HidDeviceSetReport(HID_REPORT_TYPE_INPUT, BUTTONS_RPT_ID_INPUT, report_vector.data(),
                                 report_vector.size());
  }

  device_->Bind();

  auto sync_client = GetSyncClient();

  // Get the report descriptor.
  fidl::WireResult<fuchsia_input_report::InputDevice::GetDescriptor> result =
      sync_client.GetDescriptor();
  ASSERT_OK(result.status());
  fuchsia_input_report::wire::DeviceDescriptor& desc = result->descriptor;
  ASSERT_TRUE(desc.has_consumer_control());
  ASSERT_TRUE(desc.consumer_control().has_input());
  fuchsia_input_report::wire::ConsumerControlInputDescriptor& consumer_control_desc =
      desc.consumer_control().input();
  ASSERT_TRUE(consumer_control_desc.has_buttons());
  ASSERT_EQ(5, consumer_control_desc.buttons().count());

  ASSERT_EQ(consumer_control_desc.buttons()[0],
            fuchsia_input_report::wire::ConsumerControlButton::kVolumeUp);
  ASSERT_EQ(consumer_control_desc.buttons()[1],
            fuchsia_input_report::wire::ConsumerControlButton::kVolumeDown);
  ASSERT_EQ(consumer_control_desc.buttons()[2],
            fuchsia_input_report::wire::ConsumerControlButton::kReboot);
  ASSERT_EQ(consumer_control_desc.buttons()[3],
            fuchsia_input_report::wire::ConsumerControlButton::kCameraDisable);
  ASSERT_EQ(consumer_control_desc.buttons()[4],
            fuchsia_input_report::wire::ConsumerControlButton::kMicMute);

  // Get an InputReportsReader.
  fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
  {
    auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
    ASSERT_OK(endpoints.status_value());

    auto result = sync_client.GetInputReportsReader(std::move(endpoints->server));
    ASSERT_OK(result.status());
    reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(
        std::move(endpoints->client));
    ASSERT_OK(device_->WaitForNextReader(zx::duration::infinite()));
  }

  // Create another report.
  {
    struct buttons_input_rpt report = {};
    report.rpt_id = BUTTONS_RPT_ID_INPUT;
    fill_button_in_report(BUTTONS_ID_VOLUME_UP, true, &report);
    fill_button_in_report(BUTTONS_ID_FDR, true, &report);
    fill_button_in_report(BUTTONS_ID_MIC_MUTE, true, &report);

    std::vector<uint8_t> report_vector(reinterpret_cast<uint8_t*>(&report),
                                       reinterpret_cast<uint8_t*>(&report) + sizeof(report));
    fake_hid_.SendReport(report_vector);
  }

  // Get the report.
  auto report_result = reader.ReadInputReports();
  ASSERT_OK(report_result.status());

  const fidl::VectorView<fuchsia_input_report::wire::InputReport>& reports =
      report_result->result.response().reports;
  ASSERT_EQ(2, reports.count());

  // Check the initial report.
  {
    ASSERT_TRUE(reports[0].has_consumer_control());
    const auto& report = reports[0].consumer_control();
    EXPECT_TRUE(report.has_pressed_buttons());
    EXPECT_EQ(0, report.pressed_buttons().count());
  }

  // Check the second report.
  {
    ASSERT_TRUE(reports[1].has_consumer_control());
    const auto& report = reports[1].consumer_control();
    EXPECT_TRUE(report.has_pressed_buttons());
    EXPECT_EQ(3, report.pressed_buttons().count());

    EXPECT_EQ(report.pressed_buttons()[0],
              fuchsia_input_report::wire::ConsumerControlButton::kVolumeUp);
    EXPECT_EQ(report.pressed_buttons()[1],
              fuchsia_input_report::wire::ConsumerControlButton::kReboot);
    EXPECT_EQ(report.pressed_buttons()[2],
              fuchsia_input_report::wire::ConsumerControlButton::kMicMute);
  }
}

TEST_F(HidDevTest, ConsumerControlTwoClientsTest) {
  {
    const uint8_t* descriptor;
    size_t descriptor_size = get_buttons_report_desc(&descriptor);
    std::vector<uint8_t> desc_vector(descriptor, descriptor + descriptor_size);
    fake_hid_.SetReportDesc(desc_vector);
  }

  // Create the initial report that will be queried on DdkOpen().
  {
    struct buttons_input_rpt report = {};
    report.rpt_id = BUTTONS_RPT_ID_INPUT;
    std::vector<uint8_t> report_vector(reinterpret_cast<uint8_t*>(&report),
                                       reinterpret_cast<uint8_t*>(&report) + sizeof(report));
    fake_hid_.HidDeviceSetReport(HID_REPORT_TYPE_INPUT, BUTTONS_RPT_ID_INPUT, report_vector.data(),
                                 report_vector.size());
  }

  device_->Bind();

  // Open the device.
  auto client = GetSyncClient();

  // Get an input reader and check reports.
  {
    // Get an InputReportsReader.
    fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
    {
      auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
      ASSERT_OK(endpoints.status_value());

      auto result = client.GetInputReportsReader(std::move(endpoints->server));
      ASSERT_OK(result.status());
      reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(
          std::move(endpoints->client));
    }

    auto report_result = reader.ReadInputReports();
    ASSERT_OK(report_result.status());
    const fidl::VectorView<fuchsia_input_report::wire::InputReport>& reports =
        report_result->result.response().reports;
    ASSERT_EQ(1, reports.count());

    ASSERT_TRUE(reports[0].has_consumer_control());
    const auto& report = reports[0].consumer_control();
    EXPECT_TRUE(report.has_pressed_buttons());
    EXPECT_EQ(0, report.pressed_buttons().count());
  }

  {
    // Get an InputReportsReader.
    fidl::WireSyncClient<fuchsia_input_report::InputReportsReader> reader;
    {
      auto endpoints = fidl::CreateEndpoints<fuchsia_input_report::InputReportsReader>();
      ASSERT_OK(endpoints.status_value());

      auto result = client.GetInputReportsReader(std::move(endpoints->server));
      ASSERT_OK(result.status());
      reader = fidl::WireSyncClient<fuchsia_input_report::InputReportsReader>(
          std::move(endpoints->client));
      ASSERT_OK(device_->WaitForNextReader(zx::duration::infinite()));
    }

    auto report_result = reader.ReadInputReports();
    ASSERT_OK(report_result.status());
    const fidl::VectorView<fuchsia_input_report::wire::InputReport>& reports =
        report_result->result.response().reports;
    ASSERT_EQ(1, reports.count());

    ASSERT_TRUE(reports[0].has_consumer_control());
    const auto& report = reports[0].consumer_control();
    EXPECT_TRUE(report.has_pressed_buttons());
    EXPECT_EQ(0, report.pressed_buttons().count());
  }
}

TEST_F(HidDevTest, TouchLatencyMeasurements) {
  const uint8_t* report_desc;
  const size_t desc_len = get_gt92xx_report_desc(&report_desc);
  std::vector<uint8_t> desc(report_desc, report_desc + desc_len);
  fake_hid_.SetReportDesc(desc);

  device_->Bind();

  const zx::vmo inspect_vmo = ddk_.TakeInspectVmo();
  ASSERT_TRUE(inspect_vmo.is_valid());

  // Send five reports, and verify that the inspect stats make sense.
  gt92xx_touch_t report = {};
  const uint8_t* report_ptr = reinterpret_cast<uint8_t*>(&report);
  const std::vector<uint8_t> report_vector(report_ptr, report_ptr + sizeof(report));

  // Add some additional computed latency to make the results more realistic.
  const zx_time_t timestamp = zx_clock_get_monotonic() - ZX_MSEC(15);

  fake_hid_.SendReport(report_vector, timestamp);
  fake_hid_.SendReport(report_vector, timestamp);
  fake_hid_.SendReport(report_vector, timestamp);
  fake_hid_.SendReport(report_vector, timestamp);
  fake_hid_.SendReport(report_vector, timestamp - ZX_MSEC(5));

  inspect::InspectTestHelper inspector;
  inspector.ReadInspect(inspect_vmo);

  const inspect::Hierarchy* root = inspector.hierarchy().GetByPath({"hid-input-report-touch"});
  ASSERT_NOT_NULL(root);

  const auto* latency_histogram =
      root->node().get_property<inspect::UintArrayValue>("latency_histogram_usecs");
  ASSERT_NOT_NULL(latency_histogram);
  uint64_t latency_bucket_sum = 0;
  for (const inspect::UintArrayValue::HistogramBucket& bucket : latency_histogram->GetBuckets()) {
    latency_bucket_sum += bucket.count;
  }

  EXPECT_EQ(latency_bucket_sum, 5);

  const auto* average_latency =
      root->node().get_property<inspect::UintPropertyValue>("average_latency_usecs");
  ASSERT_NOT_NULL(average_latency);

  const auto* max_latency =
      root->node().get_property<inspect::UintPropertyValue>("max_latency_usecs");
  ASSERT_NOT_NULL(max_latency);

  EXPECT_GE(max_latency->value(), average_latency->value());
}

TEST_F(HidDevTest, InspectDeviceTypes) {
  size_t desc_len;
  const uint8_t* report_desc = get_paradise_touch_report_desc(&desc_len);
  std::vector<uint8_t> desc(report_desc, report_desc + desc_len);
  fake_hid_.SetReportDesc(desc);

  device_->Bind();

  const zx::vmo inspect_vmo = ddk_.TakeInspectVmo();
  ASSERT_TRUE(inspect_vmo.is_valid());

  inspect::InspectTestHelper inspector;
  inspector.ReadInspect(inspect_vmo);

  const inspect::Hierarchy* root =
      inspector.hierarchy().GetByPath({"hid-input-report-touch,mouse"});
  ASSERT_NOT_NULL(root);

  const auto* device_types =
      root->node().get_property<inspect::StringPropertyValue>("device_types");
  ASSERT_NOT_NULL(device_types);

  EXPECT_STR_EQ(device_types->value().c_str(), "touch,mouse");
}

}  // namespace hid_input_report_dev
