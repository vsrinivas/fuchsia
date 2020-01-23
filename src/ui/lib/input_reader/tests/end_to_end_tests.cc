// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include <hid/boot.h>
#include <hid/buttons.h>
#include <hid/egalax.h>
#include <hid/paradise.h>
#include <hid/usages.h>

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "src/ui/lib/input_reader/input_reader.h"
#include "src/ui/lib/input_reader/tests/mock_device_watcher.h"
#include "src/ui/lib/input_reader/tests/mock_hid_decoder.h"
#include "src/ui/lib/input_reader/tests/sensor_test_data.h"
#include "src/ui/testing/mock_input_device.h"
#include "src/ui/testing/mock_input_device_registry.h"

namespace ui_input {

namespace {

using MockInputDevice = ui_input::test::MockInputDevice;
using MockInputDeviceRegistry = ui_input::test::MockInputDeviceRegistry;

// This fixture sets up a |MockDeviceWatcher| so that tests can add mock
// devices.
class ReaderInterpreterTest : public gtest::TestLoopFixture {
 protected:
  // Starts an |InputReader| with a |MockDeviceWatcher| and saves it locally so
  // that |MockHidDecoder|s can be added to it.
  void StartInputReader(InputReader* input_reader) {
    auto device_watcher = std::make_unique<MockDeviceWatcher>();
    device_watcher_ = device_watcher->GetWeakPtr();
    input_reader->Start(std::move(device_watcher));
  }

  // Creates a |MockHidDecoder| with the supplied |args| and adds it to the
  // |MockDeviceWatcher|, returning an |fxl::WeakPtr| to the new
  // |MockHidDecoder|.
  template <class... Args>
  fxl::WeakPtr<MockHidDecoder> AddDevice(Args&&... args) {
    auto device = std::make_unique<MockHidDecoder>(std::forward<Args>(args)...);
    auto weak_device = device->GetWeakPtr();
    device_watcher_->AddDevice(std::move(device));
    return weak_device;
  }

 private:
  fxl::WeakPtr<MockDeviceWatcher> device_watcher_;
};

// This fixture sets up a |MockInputDeviceRegistry| and an |InputReader| in
// addition to the |MockDeviceWatcher| provided by |ReaderInterpreterTest| so
// that tests can additionally verify the reports seen by the registry.
class ReaderInterpreterInputTest : public ReaderInterpreterTest {
 protected:
  ReaderInterpreterInputTest()
      : registry_(nullptr,
                  [this](fuchsia::ui::input::InputReport report) {
                    ++report_count_;
                    last_report_ = std::move(report);
                  }),
        input_reader_(&registry_) {}

  int report_count_ = 0;
  fuchsia::ui::input::InputReport last_report_;

 private:
  void SetUp() override { StartInputReader(&input_reader_); }

  MockInputDeviceRegistry registry_;
  InputReader input_reader_;
};

}  // namespace

TEST_F(ReaderInterpreterInputTest, BootMouse) {
  // Create the boot mouse report descriptor.
  size_t desc_len;
  const uint8_t* desc_data = get_boot_mouse_report_desc(&desc_len);
  ASSERT_TRUE(desc_len > 0);
  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create the device.
  fxl::WeakPtr<MockHidDecoder> device = AddDevice(report_descriptor, HidDecoder::BootMode::MOUSE);
  RunLoopUntilIdle();

  // Create a single boot mouse report.
  hid_boot_mouse_report_t mouse_report = {};
  mouse_report.rel_x = 50;
  mouse_report.rel_y = 100;
  uint8_t* mouse_report_bytes = reinterpret_cast<uint8_t*>(&mouse_report);
  std::vector<uint8_t> report(mouse_report_bytes, mouse_report_bytes + sizeof(mouse_report));

  // Send the boot mouse report.
  device->SetHidDecoderRead(report, sizeof(mouse_report));
  RunLoopUntilIdle();

  ASSERT_TRUE(last_report_.mouse);
  EXPECT_EQ(last_report_.mouse->rel_x, 50);
  EXPECT_EQ(last_report_.mouse->rel_y, 100);
}

// Tests that InputReader reads multiple reports at once.
TEST_F(ReaderInterpreterInputTest, BootMouseMultiReports) {
  // Create the boot mouse report descriptor.
  size_t desc_len;
  const uint8_t* desc_data = get_boot_mouse_report_desc(&desc_len);
  ASSERT_TRUE(desc_len > 0);
  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create the device.
  fxl::WeakPtr<MockHidDecoder> device = AddDevice(report_descriptor, HidDecoder::BootMode::MOUSE);
  RunLoopUntilIdle();

  // Create multiple reports.
  const size_t kNumMouseReports = 5;
  hid_boot_mouse_report_t mouse_reports[kNumMouseReports] = {};
  uint8_t* mouse_report_bytes = reinterpret_cast<uint8_t*>(&mouse_reports);
  std::vector<uint8_t> reports(mouse_report_bytes, mouse_report_bytes + sizeof(mouse_reports));

  // Send the boot mouse report.
  device->SetHidDecoderRead(reports, sizeof(mouse_reports));
  RunLoopUntilIdle();

  ASSERT_EQ(kNumMouseReports, static_cast<size_t>(report_count_));
}

TEST_F(ReaderInterpreterInputTest, BootKeyboard) {
  // Create the boot keyboard report descriptor.
  size_t desc_len;
  const uint8_t* desc_data = get_boot_kbd_report_desc(&desc_len);
  ASSERT_TRUE(desc_len > 0);
  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create the device.
  fxl::WeakPtr<MockHidDecoder> device =
      AddDevice(report_descriptor, HidDecoder::BootMode::KEYBOARD);
  RunLoopUntilIdle();

  // A keyboard report is 8 bytes long, with bytes 3-8 containing HID usage
  // codes.
  device->SetHidDecoderRead({0, 0, HID_USAGE_KEY_A, 0, 0, 0, 0, 0}, 8);

  RunLoopUntilIdle();
  EXPECT_EQ(1, report_count_);
  ASSERT_TRUE(last_report_.keyboard);
  EXPECT_EQ(std::vector<uint32_t>{HID_USAGE_KEY_A}, last_report_.keyboard->pressed_keys);

  device->SetHidDecoderRead({0, 0, HID_USAGE_KEY_A, HID_USAGE_KEY_Z, 0, 0, 0, 0}, 8);
  RunLoopUntilIdle();
  EXPECT_EQ(2, report_count_);
  EXPECT_EQ(std::multiset<uint32_t>({HID_USAGE_KEY_A, HID_USAGE_KEY_Z}),
            std::multiset<uint32_t>(last_report_.keyboard->pressed_keys.begin(),
                                    last_report_.keyboard->pressed_keys.end()));

  device->SetHidDecoderRead({0, 0, HID_USAGE_KEY_Z, 0, 0, 0, 0, 0}, 8);
  RunLoopUntilIdle();
  EXPECT_EQ(std::vector<uint32_t>{HID_USAGE_KEY_Z}, last_report_.keyboard->pressed_keys);

  // expect that if the keyboard sends a rollover error, we keep the previous pressed keys
  device->SetHidDecoderRead(
      {HID_USAGE_KEY_ERROR_ROLLOVER, 0, HID_USAGE_KEY_ERROR_ROLLOVER, HID_USAGE_KEY_ERROR_ROLLOVER,
       HID_USAGE_KEY_ERROR_ROLLOVER, HID_USAGE_KEY_ERROR_ROLLOVER, HID_USAGE_KEY_ERROR_ROLLOVER,
       HID_USAGE_KEY_ERROR_ROLLOVER},
      8);
  RunLoopUntilIdle();
  EXPECT_EQ(std::vector<uint32_t>{HID_USAGE_KEY_Z}, last_report_.keyboard->pressed_keys);
}

TEST_F(ReaderInterpreterInputTest, EgalaxTouchScreen) {
  // Create the egalax report descriptor.
  size_t desc_len;
  const uint8_t* desc_data = get_egalax_touch_report_desc(&desc_len);
  ASSERT_TRUE(desc_len > 0);
  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create the MockHidDecoder with our report descriptor.
  fxl::WeakPtr<MockHidDecoder> device = AddDevice(report_descriptor);
  RunLoopUntilIdle();

  // Create a single touch report.
  egalax_touch_t touch_report = {};
  touch_report.report_id = EGALAX_RPT_ID_TOUCH;
  touch_report.x = 100;
  touch_report.y = 200;
  touch_report.button_pad = 0xFF;
  uint8_t* touch_report_bytes = reinterpret_cast<uint8_t*>(&touch_report);
  std::vector<uint8_t> report(touch_report_bytes, touch_report_bytes + sizeof(touch_report));

  // Send the touch report.
  device->SetHidDecoderRead(report, sizeof(touch_report));
  RunLoopUntilIdle();

  // Check that we saw one report, and that the data was sent out correctly.
  ASSERT_EQ(1, report_count_);
  ASSERT_TRUE(last_report_.touchscreen);
  fuchsia::ui::input::Touch touch = last_report_.touchscreen->touches.at(0);
  EXPECT_TRUE(touch.finger_id = 1);
  EXPECT_TRUE(touch.x = 100);
  EXPECT_TRUE(touch.y = 200);
}

TEST_F(ReaderInterpreterInputTest, ParadiseTouchscreen) {
  // Create the paradise report descriptor.
  size_t desc_len;
  const uint8_t* desc_data = get_paradise_touch_report_desc(&desc_len);
  ASSERT_TRUE(desc_len > 0);
  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create the MockHidDecoder with our report descriptor.
  fxl::WeakPtr<MockHidDecoder> device = AddDevice(report_descriptor);
  RunLoopUntilIdle();

  std::vector<uint8_t> feature_report = device->GetLastOutputReport();
  ASSERT_EQ(3U, feature_report.size());
  uint8_t multitouch_enable_report[3] = {14, 2, 0};
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(multitouch_enable_report[i], feature_report[i]);
  }

  // Create a single touch report.
  paradise_touch_t touch_report = {};
  touch_report.rpt_id = PARADISE_RPT_ID_TOUCH;
  touch_report.contact_count = 1;
  touch_report.fingers[0].flags = 0xFF;
  touch_report.fingers[0].finger_id = 1;
  touch_report.fingers[0].x = 100;
  touch_report.fingers[0].y = 200;
  uint8_t* touch_report_bytes = reinterpret_cast<uint8_t*>(&touch_report);
  std::vector<uint8_t> report(touch_report_bytes, touch_report_bytes + sizeof(touch_report));

  // Send the touch report.
  device->SetHidDecoderRead(report, sizeof(touch_report));
  RunLoopUntilIdle();

  // Check that we saw one report, and that the data was sent out correctly.
  ASSERT_EQ(1, report_count_);
  ASSERT_TRUE(last_report_.touchscreen);
  fuchsia::ui::input::Touch touch = last_report_.touchscreen->touches.at(0);
  EXPECT_TRUE(touch.finger_id = 1);
  EXPECT_TRUE(touch.x = 100);
  EXPECT_TRUE(touch.y = 200);
}

// Test that InputReader reads multiple different sized reports based on the report ID.
TEST_F(ReaderInterpreterInputTest, TouchscreenMultiReport) {
  // Create the paradise report descriptor.
  size_t desc_len;
  const uint8_t* desc_data = get_paradise_touch_report_desc(&desc_len);
  ASSERT_TRUE(desc_len > 0);
  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create the MockHidDecoder with our report descriptor.
  fxl::WeakPtr<MockHidDecoder> device = AddDevice(report_descriptor);
  RunLoopUntilIdle();

  // Create a touch report, stylus report, touch report, stylus report.
  const size_t kReportsSize = (2 * sizeof(paradise_touch_t)) + (2 * sizeof(paradise_stylus_t));
  uint8_t reports_data[kReportsSize] = {};
  uint8_t* report_index = reports_data;
  *report_index = PARADISE_RPT_ID_TOUCH;
  report_index += sizeof(paradise_touch_t);

  *report_index = PARADISE_RPT_ID_STYLUS;
  report_index += sizeof(paradise_stylus_t);

  *report_index = PARADISE_RPT_ID_TOUCH;
  report_index += sizeof(paradise_touch_t);

  *report_index = PARADISE_RPT_ID_STYLUS;
  report_index += sizeof(paradise_stylus_t);
  ASSERT_EQ(static_cast<size_t>(report_index - reports_data), kReportsSize);

  std::vector<uint8_t> report(reports_data, report_index);

  // Send the reports.
  device->SetHidDecoderRead(report, report.size());
  RunLoopUntilIdle();

  // Check that we saw all of the reports.
  ASSERT_EQ(4, report_count_);
}

TEST_F(ReaderInterpreterInputTest, ParadiseTouchpad) {
  // Create the paradise report descriptor.
  size_t desc_len;
  const uint8_t* desc_data = get_paradise_touchpad_v1_report_desc(&desc_len);
  ASSERT_TRUE(desc_len > 0);
  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create the MockHidDecoder with our report descriptor.
  fxl::WeakPtr<MockHidDecoder> device = AddDevice(report_descriptor);
  RunLoopUntilIdle();

  // Create a single touch report.
  paradise_touchpad_v1_t touch_report = {};
  touch_report.report_id = 1;
  touch_report.contact_count = 1;
  touch_report.fingers[0].tip_switch = 1;
  touch_report.fingers[0].id = 1;
  touch_report.fingers[0].x = 100;
  touch_report.fingers[0].y = 200;
  uint8_t* touch_report_bytes = reinterpret_cast<uint8_t*>(&touch_report);
  std::vector<uint8_t> report(touch_report_bytes, touch_report_bytes + sizeof(touch_report));

  // Send the touch report.
  device->SetHidDecoderRead(report, sizeof(touch_report));
  RunLoopUntilIdle();

  // Check that we saw one report. Mice are relative so we shouldn't see
  // any movement yet.
  ASSERT_EQ(1, report_count_);
  ASSERT_TRUE(last_report_.mouse);
  EXPECT_EQ(0, last_report_.mouse->rel_x);
  EXPECT_EQ(0, last_report_.mouse->rel_y);

  // Send a second touch report.
  touch_report.fingers[0].x = 500;
  touch_report.fingers[0].y = 1000;
  report = std::vector<uint8_t>(touch_report_bytes, touch_report_bytes + sizeof(touch_report));

  // Send the touch report.
  device->SetHidDecoderRead(report, sizeof(touch_report));
  RunLoopUntilIdle();

  ASSERT_EQ(2, report_count_);
  ASSERT_TRUE(last_report_.mouse);
  // These output values were hand calculated by converting the relative x
  // and y to the units described by the paradise report.
  EXPECT_EQ(78, last_report_.mouse->rel_x);
  EXPECT_EQ(156, last_report_.mouse->rel_y);
}

TEST_F(ReaderInterpreterInputTest, SensorTest) {
  // Create the paradise report descriptor.
  size_t desc_len = sizeof(lightmeter_report_desc);
  const uint8_t* desc_data = lightmeter_report_desc;
  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create the MockHidDecoder with our report descriptor.
  fxl::WeakPtr<MockHidDecoder> device = AddDevice(report_descriptor);
  RunLoopUntilIdle();

  // Create a single light report.
  uint8_t report_data[] = {
      0x04,        // Report ID
      0x12, 0x24,  // Illuminance
  };
  std::vector<uint8_t> report(report_data, report_data + sizeof(report_data));

  // Send the touch report.
  device->SetHidDecoderRead(report, sizeof(report_data));
  RunLoopUntilIdle();

  // Check that the report matches.
  ASSERT_EQ(1, report_count_);
  ASSERT_TRUE(last_report_.sensor);
  EXPECT_EQ(0x2412, last_report_.sensor->scalar());
}

TEST_F(ReaderInterpreterInputTest, MediaButtonsTest) {
  // Create the paradise report descriptor.
  size_t desc_len;
  const uint8_t* desc_data;
  desc_len = get_buttons_report_desc(&desc_data);

  std::vector<uint8_t> report_descriptor(desc_data, desc_data + desc_len);

  // Create a single buttons report.
  buttons_input_rpt_t report_data = {};
  report_data.rpt_id = BUTTONS_RPT_ID_INPUT;
  report_data.volume_up = true;
  report_data.volume_down = false;
  report_data.reset = true;
  report_data.mute = true;
  uint8_t* report_data_ptr = reinterpret_cast<uint8_t*>(&report_data);
  std::vector<uint8_t> report(report_data_ptr, report_data_ptr + sizeof(report_data));

  // Create the MockHidDecoder with our report descriptor and initial report
  fxl::WeakPtr<MockHidDecoder> device = AddDevice(report_descriptor, report);
  RunLoopUntilIdle();

  // Check that the initial report has already been sent. This is to query
  // the inital button state.
  ASSERT_EQ(1, report_count_);
  ASSERT_TRUE(last_report_.media_buttons);
  EXPECT_TRUE(last_report_.media_buttons->volume_up);
  EXPECT_FALSE(last_report_.media_buttons->volume_down);
  EXPECT_TRUE(last_report_.media_buttons->reset);
  EXPECT_TRUE(last_report_.media_buttons->mic_mute);

  // Send another touch report
  report_data.volume_up = false;
  report_data.volume_down = true;
  report_data.reset = false;
  report_data.mute = false;
  report = std::vector<uint8_t>(report_data_ptr, report_data_ptr + sizeof(report_data));
  device->SetHidDecoderRead(report, sizeof(report_data));
  RunLoopUntilIdle();

  // Check that the report matches.
  ASSERT_EQ(2, report_count_);
  ASSERT_TRUE(last_report_.media_buttons);
  EXPECT_FALSE(last_report_.media_buttons->volume_up);
  EXPECT_TRUE(last_report_.media_buttons->volume_down);
  EXPECT_FALSE(last_report_.media_buttons->reset);
  EXPECT_FALSE(last_report_.media_buttons->mic_mute);
}

}  // namespace ui_input
