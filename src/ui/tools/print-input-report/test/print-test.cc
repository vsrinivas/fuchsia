// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>

#include <string>
#include <vector>

#include <ddk/device.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <gtest/gtest.h>
#include <hid/usages.h>

#include "src/ui/lib/hid-input-report/fidl.h"
#include "src/ui/tools/print-input-report/devices.h"
#include "src/ui/tools/print-input-report/printer.h"

namespace test {

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

class FakePrinter : public print_input_report::Printer {
 public:
  void RealPrint(const char* format, va_list argptr) override {
    char buf[kMaxBufLen];
    vsprintf(buf, format, argptr);

    ASSERT_LT(current_string_index_, expected_strings_.size());
    const std::string& expected = expected_strings_[current_string_index_];
    current_string_index_++;

    // Check that we match the expected string.
    ASSERT_GT(expected.size(), indent_);
    int cmp = strcmp(buf, expected.c_str());
    if (cmp != 0) {
      printf("Wanted string: '%s'\n", expected.c_str());
      printf("Saw string:    '%s'\n", buf);
      ASSERT_TRUE(false);
    }

    // Print the string for easy debugging.
    vprintf(format, argptr);

    va_end(argptr);
  }

  void SetExpectedStrings(const std::vector<std::string>& strings) {
    current_string_index_ = 0;
    expected_strings_ = strings;
  }

 private:
  static constexpr size_t kMaxBufLen = 1024;
  size_t current_string_index_ = 0;
  std::vector<std::string> expected_strings_;
};

// This class fakes the InputReport driver by implementing the InputDevice interface.
// The test client can use |SetReport| and |SetDescriptor| to send reports/descriptors
// through the interface.
class FakeDevice final : public fuchsia_input_report::InputDevice::Interface {
 public:
  FakeDevice() : lock_() { zx::event::create(0, &reports_event_); }
  virtual void GetReportsEvent(GetReportsEventCompleter::Sync completer) override {
    fbl::AutoLock lock(&lock_);
    zx::event new_event;
    zx_status_t status = reports_event_.duplicate(ZX_RIGHTS_BASIC, &new_event);
    completer.Reply(status, std::move(new_event));
  }

  virtual void GetReports(GetReportsCompleter::Sync completer) override {
    fbl::AutoLock lock(&lock_);
    hid_input_report::FidlReport fidl;
    zx_status_t status = hid_input_report::SetFidlReport(report_, &fidl);
    if (status != ZX_OK) {
      completer.Reply(fidl::VectorView<fuchsia_input_report::InputReport>(nullptr, 0));
      return;
    }

    fuchsia_input_report::InputReport report = fidl.builder.view();
    reports_event_.signal(DEV_STATE_READABLE, 0);
    completer.Reply(fidl::VectorView<fuchsia_input_report::InputReport>(&report, 1));
  }

  // Sets the fake's report, which will be read with |GetReports|. This also
  // triggers the |reports_events_| signal which wakes up any clients waiting
  // for report dta.
  void SetReport(hid_input_report::Report report) {
    fbl::AutoLock lock(&lock_);
    report_ = report;
    reports_event_.signal(0, DEV_STATE_READABLE);
  }

  void GetDescriptor(GetDescriptorCompleter::Sync completer) override {
    fbl::AutoLock lock(&lock_);
    hid_input_report::FidlDescriptor fidl;
    ASSERT_EQ(hid_input_report::SetFidlDescriptor(descriptor_, &fidl), ZX_OK);

    fuchsia_input_report::DeviceDescriptor descriptor = fidl.builder.view();
    completer.Reply(std::move(descriptor));
  }

  // Sets the fake's descriptor, which will be read with |GetDescriptor|.
  void SetDescriptor(hid_input_report::ReportDescriptor descriptor) {
    fbl::AutoLock lock(&lock_);
    descriptor_ = descriptor;
  }

 private:
  // This lock makes the class thread-safe, which is important because setting the
  // reports and handling the FIDL calls happen on seperate threads.
  fbl::Mutex lock_ = {};
  zx::event reports_event_ __TA_GUARDED(lock_);
  hid_input_report::Report report_ __TA_GUARDED(lock_) = {};
  hid_input_report::ReportDescriptor descriptor_ __TA_GUARDED(lock_) = {};
};

class PrintInputReport : public ::testing::Test {
 protected:
  virtual void SetUp() {
    // Make the channels and the fake device.
    zx::channel token_server, token_client;
    ASSERT_EQ(zx::channel::create(0, &token_server, &token_client), ZX_OK);
    fake_device_ = std::make_unique<FakeDevice>();

    // Make and run the thread for the fake device's FIDL interface. This is necessary
    // because the interface is asyncronous and will block the dispatcher.
    // TODO(dgilhooley): When LLCPP supports async clients, make this test single threaded
    // with a dispatcher.
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test-print-input-report-loop"), ZX_OK);
    fidl::Bind(loop_->dispatcher(), std::move(token_server), fake_device_.get());

    // Make the client.
    client_ = fuchsia_input_report::InputDevice::SyncClient(std::move(token_client));
  }

  virtual void TearDown() {
    loop_->Quit();
    loop_->JoinThreads();
  }

  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<FakeDevice> fake_device_;
  std::optional<fuchsia_input_report::InputDevice::SyncClient> client_;
};

TEST_F(PrintInputReport, PrintMouseReport) {
  hid_input_report::MouseReport mouse = {};
  mouse.movement_x = 100;
  mouse.movement_y = 200;

  mouse.num_buttons_pressed = 3;
  mouse.buttons_pressed[0] = 1;
  mouse.buttons_pressed[1] = 10;
  mouse.buttons_pressed[2] = 5;

  hid_input_report::Report report;
  report.report = mouse;

  fake_device_->SetReport(report);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Movement x: 00000100\n",
      "Movement y: 00000200\n",
      "Button 01 pressed\n",
      "Button 10 pressed\n",
      "Button 05 pressed\n",
      "\n",
  });
  print_input_report::PrintInputReport(&printer, &client_.value(), 1);
}

TEST_F(PrintInputReport, PrintMouseDescriptor) {
  hid_input_report::MouseDescriptor mouse = {};
  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::DISTANCE;
  axis.range.min = -100;
  axis.range.max = -100;
  mouse.movement_x = axis;

  axis.unit = fuchsia_input_report::Unit::NONE;
  axis.range.min = -200;
  axis.range.max = -200;
  mouse.movement_y = axis;

  mouse.num_buttons = 3;
  mouse.buttons[0] = 1;
  mouse.buttons[1] = 10;
  mouse.buttons[2] = 5;

  hid_input_report::ReportDescriptor descriptor;
  descriptor.descriptor = mouse;

  fake_device_->SetDescriptor(descriptor);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Mouse Descriptor:\n",
      "  Movement X:\n",
      "    Unit: DISTANCE\n",
      "    Min:      -100\n",
      "    Max:      -100\n",
      "  Movement Y:\n",
      "    Unit:     NONE\n",
      "    Min:      -200\n",
      "    Max:      -200\n",
      "  Button: 1\n",
      "  Button: 10\n",
      "  Button: 5\n",
  });

  print_input_report::PrintInputDescriptor(&printer, &client_.value());
}

TEST_F(PrintInputReport, PrintSensorDescriptor) {
  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::LINEAR_VELOCITY;
  axis.range.min = 0;
  axis.range.max = 1000;

  hid_input_report::SensorDescriptor sensor_desc = {};
  sensor_desc.values[0].axis = axis;
  sensor_desc.values[0].type = fuchsia_input_report::SensorType::ACCELEROMETER_X;

  axis.unit = fuchsia_input_report::Unit::LUMINOUS_FLUX;
  sensor_desc.values[1].axis = axis;
  sensor_desc.values[1].type = fuchsia_input_report::SensorType::LIGHT_ILLUMINANCE;
  sensor_desc.num_values = 2;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = sensor_desc;

  fake_device_->SetDescriptor(desc);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Sensor Descriptor:\n",
      "  Value 00:\n",
      "    SensorType: ACCELEROMETER_X\n",
      "    Unit: LINEAR_VELOCITY\n",
      "    Min:         0\n",
      "    Max:      1000\n",
      "  Value 01:\n",
      "    SensorType: LIGHT_ILLUMINANCE\n",
      "    Unit: LUMINOUS_FLUX\n",
      "    Min:         0\n",
      "    Max:      1000\n",
  });

  print_input_report::PrintInputDescriptor(&printer, &client_.value());
}

TEST_F(PrintInputReport, PrintSensorReport) {
  hid_input_report::SensorReport sensor_report = {};
  sensor_report.values[0] = 100;
  sensor_report.values[1] = -100;
  sensor_report.num_values = 2;

  hid_input_report::Report report;
  report.report = sensor_report;

  fake_device_->SetReport(report);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Sensor[00]: 00000100\n",
      "Sensor[01]: -0000100\n",
      "\n",
  });

  print_input_report::PrintInputReport(&printer, &client_.value(), 1);
}

TEST_F(PrintInputReport, PrintTouchDescriptor) {
  hid_input_report::TouchDescriptor touch_desc = {};
  touch_desc.touch_type = fuchsia_input_report::TouchType::TOUCHSCREEN;

  touch_desc.max_contacts = 100;

  fuchsia_input_report::Axis axis;
  axis.unit = fuchsia_input_report::Unit::NONE;
  axis.range.min = 0;
  axis.range.max = 300;

  touch_desc.contacts[0].position_x = axis;

  axis.range.max = 500;
  touch_desc.contacts[0].position_y = axis;

  axis.range.max = 100;
  touch_desc.contacts[0].pressure = axis;

  touch_desc.num_contacts = 1;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = touch_desc;

  fake_device_->SetDescriptor(desc);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Touch Descriptor:\n",
      "  Touch Type: TOUCHSCREEN\n",
      "  Max Contacts: 100\n",
      "  Contact: 00\n",
      "    Position X:\n",
      "      Unit:     NONE\n",
      "      Min:         0\n",
      "      Max:       300\n",
      "    Position Y:\n",
      "      Unit:     NONE\n",
      "      Min:         0\n",
      "      Max:       500\n",
      "    Pressure:\n",
      "      Unit:     NONE\n",
      "      Min:         0\n",
      "      Max:       100\n",
  });

  print_input_report::PrintInputDescriptor(&printer, &client_.value());
}

TEST_F(PrintInputReport, PrintTouchReport) {
  hid_input_report::TouchReport touch_report = {};

  touch_report.num_contacts = 1;

  touch_report.contacts[0].contact_id = 10;
  touch_report.contacts[0].position_x = 123;
  touch_report.contacts[0].position_y = 234;
  touch_report.contacts[0].pressure = 345;
  touch_report.contacts[0].contact_width = 678;
  touch_report.contacts[0].contact_height = 789;

  hid_input_report::Report report;
  report.report = touch_report;

  fake_device_->SetReport(report);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Contact ID: 10\n",
      "  Position X:     00000123\n",
      "  Position Y:     00000234\n",
      "  Pressure:       00000345\n",
      "  Contact Width:  00000678\n",
      "  Contact Height: 00000789\n",
      "\n",
  });

  print_input_report::PrintInputReport(&printer, &client_.value(), 1);
}

TEST_F(PrintInputReport, PrintKeyboardDescriptor) {
  hid_input_report::KeyboardDescriptor keyboard_desc = {};

  keyboard_desc.num_keys = 3;
  keyboard_desc.keys[0] = llcpp::fuchsia::ui::input2::Key::A;
  keyboard_desc.keys[1] = llcpp::fuchsia::ui::input2::Key::UP;
  keyboard_desc.keys[2] = llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT;

  hid_input_report::ReportDescriptor desc;
  desc.descriptor = keyboard_desc;

  fake_device_->SetDescriptor(desc);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Keyboard Descriptor:\n",
      "  Key:        1\n",
      "  Key:       79\n",
      "  Key:       82\n",
  });

  print_input_report::PrintInputDescriptor(&printer, &client_.value());
}

TEST_F(PrintInputReport, PrintKeyboardReport) {
  hid_input_report::KeyboardReport keyboard_report = {};

  keyboard_report.num_pressed_keys = 3;
  keyboard_report.pressed_keys[0] = llcpp::fuchsia::ui::input2::Key::A;
  keyboard_report.pressed_keys[1] = llcpp::fuchsia::ui::input2::Key::UP;
  keyboard_report.pressed_keys[2] = llcpp::fuchsia::ui::input2::Key::LEFT_SHIFT;

  hid_input_report::Report report;
  report.report = keyboard_report;

  fake_device_->SetReport(report);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Keyboard Report\n",
      "  Key:        1\n",
      "  Key:       79\n",
      "  Key:       82\n",
      "\n",
  });

  print_input_report::PrintInputReport(&printer, &client_.value(), 1);
}

TEST_F(PrintInputReport, PrintKeyboardReportNoKeys) {
  hid_input_report::KeyboardReport keyboard_report = {};

  keyboard_report.num_pressed_keys = 0;

  hid_input_report::Report report;
  report.report = keyboard_report;

  fake_device_->SetReport(report);

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Keyboard Report\n",
      "  No keys pressed\n",
      "\n",
  });

  print_input_report::PrintInputReport(&printer, &client_.value(), 1);
}

}  // namespace test
