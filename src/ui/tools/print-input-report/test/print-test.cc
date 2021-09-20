// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/device.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/sync/completion.h>

#include <string>
#include <vector>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <gtest/gtest.h>
#include <hid/usages.h>

#include "src/ui/input/testing/fake_input_report_device/fake.h"
#include "src/ui/tools/print-input-report/devices.h"
#include "src/ui/tools/print-input-report/printer.h"

namespace test {

namespace fuchsia_input_report = fuchsia_input_report;

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

  void AssertSawAllStrings() { ASSERT_EQ(current_string_index_, expected_strings_.size()); }

 private:
  static constexpr size_t kMaxBufLen = 1024;
  size_t current_string_index_ = 0;
  std::vector<std::string> expected_strings_;
};

class PrintInputReport : public testing::Test {
 protected:
  virtual void SetUp() {
    // Make the channels and the fake device.
    zx::channel token_server, token_client;
    ASSERT_EQ(zx::channel::create(0, &token_server, &token_client), ZX_OK);

    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);

    fake_device_ = std::make_unique<fake_input_report_device::FakeInputDevice>(
        fidl::InterfaceRequest<fuchsia::input::report::InputDevice>(std::move(token_server)),
        loop_->dispatcher());

    client_.emplace(fidl::ClientEnd<fuchsia_input_report::InputDevice>(std::move(token_client)),
                    loop_->dispatcher());
  }

  virtual void TearDown() { loop_->Quit(); }

  std::unique_ptr<fake_input_report_device::FakeInputDevice> fake_device_;
  std::unique_ptr<async::Loop> loop_;
  std::optional<fidl::WireSharedClient<fuchsia_input_report::InputDevice>> client_;
};

TEST_F(PrintInputReport, PrintMouseInputReport) {
  fuchsia::input::report::InputReport report;
  report.mutable_mouse()->set_movement_x(100);
  report.mutable_mouse()->set_movement_y(200);
  report.mutable_mouse()->set_position_x(300);
  report.mutable_mouse()->set_position_y(400);
  report.mutable_mouse()->set_scroll_v(100);
  report.mutable_mouse()->set_pressed_buttons({1, 10, 5});

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Movement x: 00000100\n",
      "Movement y: 00000200\n",
      "Position x: 00000300\n",
      "Position y: 00000400\n",
      "Scroll v: 00000100\n",
      "Button 01 pressed\n",
      "Button 10 pressed\n",
      "Button 05 pressed\n",
      "\n",
  });

  auto res = print_input_report::GetReaderClient(&client_.value(), loop_->dispatcher());
  ASSERT_EQ(res.status_value(), ZX_OK);
  auto reader = std::move(res.value());

  print_input_report::PrintInputReports("test", &printer, std::move(reader), 1);
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintMouseGetInputReport) {
  fuchsia::input::report::InputReport report;
  report.mutable_mouse()->set_movement_x(100);
  report.mutable_mouse()->set_movement_y(200);
  report.mutable_mouse()->set_position_x(300);
  report.mutable_mouse()->set_position_y(400);
  report.mutable_mouse()->set_scroll_v(100);
  report.mutable_mouse()->set_pressed_buttons({1, 10, 5});

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Movement x: 00000100\n",
      "Movement y: 00000200\n",
      "Position x: 00000300\n",
      "Position y: 00000400\n",
      "Scroll v: 00000100\n",
      "Button 01 pressed\n",
      "Button 10 pressed\n",
      "Button 05 pressed\n",
      "\n",
  });

  print_input_report::GetAndPrintInputReport("test", fuchsia_input_report::wire::DeviceType::kMouse,
                                             &printer, std::move(*client_));
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintMouseInputDescriptor) {
  auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
  auto mouse = descriptor->mutable_mouse()->mutable_input();

  fuchsia::input::report::Axis axis;
  axis.unit.type = fuchsia::input::report::UnitType::METERS;
  axis.unit.exponent = 0;
  axis.range.min = -100;
  axis.range.max = -100;
  mouse->set_movement_x(axis);

  axis.unit.type = fuchsia::input::report::UnitType::NONE;
  axis.range.min = -200;
  axis.range.max = -200;
  mouse->set_movement_y(axis);

  axis.range.min = 300;
  axis.range.max = 300;
  mouse->set_position_x(axis);

  axis.range.min = 400;
  axis.range.max = 400;
  mouse->set_position_y(axis);

  mouse->set_buttons({1, 10, 5});

  fake_device_->SetDescriptor(std::move(descriptor));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Descriptor from file: test\n",
      "Mouse Descriptor:\n",
      "  Movement X:\n",
      "    Unit:   METERS\n",
      "    Min:      -100\n",
      "    Max:      -100\n",
      "  Movement Y:\n",
      "    Unit:     NONE\n",
      "    Min:      -200\n",
      "    Max:      -200\n",
      "  Position X:\n",
      "    Unit:     NONE\n",
      "    Min:       300\n",
      "    Max:       300\n",
      "  Position Y:\n",
      "    Unit:     NONE\n",
      "    Min:       400\n",
      "    Max:       400\n",
      "  Button: 1\n",
      "  Button: 10\n",
      "  Button: 5\n",
  });

  print_input_report::PrintInputDescriptor(std::string("test"), &printer, std::move(*client_));

  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintSensorInputDescriptor) {
  auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
  auto values = descriptor->mutable_sensor()->mutable_input()->mutable_values();

  fuchsia::input::report::SensorAxis axis;
  axis.axis.unit.type = fuchsia::input::report::UnitType::SI_LINEAR_VELOCITY;
  axis.axis.unit.exponent = 0;
  axis.axis.range.min = 0;
  axis.axis.range.max = 1000;
  axis.type = fuchsia::input::report::SensorType::ACCELEROMETER_X;

  values->push_back(axis);

  axis.axis.unit.type = fuchsia::input::report::UnitType::LUX;
  axis.type = fuchsia::input::report::SensorType::LIGHT_ILLUMINANCE;

  values->push_back(axis);

  fake_device_->SetDescriptor(std::move(descriptor));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Descriptor from file: test\n",
      "Sensor Descriptor:\n",
      "  Value 00:\n",
      "    SensorType: ACCELEROMETER_X\n",
      "    Unit: SI_LINEAR_VELOCITY\n",
      "    Min:         0\n",
      "    Max:      1000\n",
      "  Value 01:\n",
      "    SensorType: LIGHT_ILLUMINANCE\n",
      "    Unit:      LUX\n",
      "    Min:         0\n",
      "    Max:      1000\n",
  });

  print_input_report::PrintInputDescriptor(std::string("test"), &printer, std::move(*client_));
  loop_->RunUntilIdle();
}

TEST_F(PrintInputReport, PrintSensorInputReport) {
  fuchsia::input::report::InputReport report;
  report.mutable_sensor()->set_values({100, -100});

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Sensor[00]: 00000100\n",
      "Sensor[01]: -0000100\n",
      "\n",
  });

  auto res = print_input_report::GetReaderClient(&client_.value(), loop_->dispatcher());
  ASSERT_EQ(res.status_value(), ZX_OK);
  auto reader = std::move(res.value());

  print_input_report::PrintInputReports("test", &printer, std::move(reader), 1);
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintSensorGetInputReport) {
  fuchsia::input::report::InputReport report;
  report.mutable_sensor()->set_values({100, -100});

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Sensor[00]: 00000100\n",
      "Sensor[01]: -0000100\n",
      "\n",
  });

  print_input_report::GetAndPrintInputReport(
      "test", fuchsia_input_report::wire::DeviceType::kSensor, &printer, std::move(*client_));
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintTouchInputDescriptor) {
  auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
  auto touch = descriptor->mutable_touch()->mutable_input();
  touch->set_touch_type(fuchsia::input::report::TouchType::TOUCHSCREEN);
  touch->set_max_contacts(100);

  fuchsia::input::report::Axis axis;
  axis.unit.type = fuchsia::input::report::UnitType::NONE;
  axis.unit.exponent = 0;
  axis.range.min = 0;
  axis.range.max = 300;

  fuchsia::input::report::ContactInputDescriptor contact;
  contact.set_position_x(axis);

  axis.range.max = 500;
  contact.set_position_y(axis);

  axis.range.max = 100;
  contact.set_pressure(axis);

  touch->mutable_contacts()->push_back(std::move(contact));

  fake_device_->SetDescriptor(std::move(descriptor));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Descriptor from file: test\n",
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

  print_input_report::PrintInputDescriptor(std::string("test"), &printer, std::move(*client_));
  loop_->RunUntilIdle();
}

TEST_F(PrintInputReport, PrintTouchInputReport) {
  fuchsia::input::report::InputReport report;
  fuchsia::input::report::ContactInputReport contact;

  contact.set_contact_id(10);
  contact.set_position_x(123);
  contact.set_position_y(234);
  contact.set_pressure(345);
  contact.set_contact_width(678);
  contact.set_contact_height(789);

  report.mutable_touch()->mutable_contacts()->push_back(std::move(contact));

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Contact ID: 10\n",
      "  Position X:     00000123\n",
      "  Position Y:     00000234\n",
      "  Pressure:       00000345\n",
      "  Contact Width:  00000678\n",
      "  Contact Height: 00000789\n",
      "\n",
  });

  auto res = print_input_report::GetReaderClient(&client_.value(), loop_->dispatcher());
  ASSERT_EQ(res.status_value(), ZX_OK);
  auto reader = std::move(res.value());

  print_input_report::PrintInputReports("test", &printer, std::move(reader), 1);
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintTouchGetInputReport) {
  fuchsia::input::report::InputReport report;
  fuchsia::input::report::ContactInputReport contact;

  contact.set_contact_id(10);
  contact.set_position_x(123);
  contact.set_position_y(234);
  contact.set_pressure(345);
  contact.set_contact_width(678);
  contact.set_contact_height(789);

  report.mutable_touch()->mutable_contacts()->push_back(std::move(contact));

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Contact ID: 10\n",
      "  Position X:     00000123\n",
      "  Position Y:     00000234\n",
      "  Pressure:       00000345\n",
      "  Contact Width:  00000678\n",
      "  Contact Height: 00000789\n",
      "\n",
  });

  print_input_report::GetAndPrintInputReport("test", fuchsia_input_report::wire::DeviceType::kTouch,
                                             &printer, std::move(*client_));
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintKeyboardDescriptor) {
  auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
  descriptor->mutable_keyboard()->mutable_input()->set_keys3(
      {fuchsia::input::Key::A, fuchsia::input::Key::UP, fuchsia::input::Key::LEFT_SHIFT});
  descriptor->mutable_keyboard()->mutable_output()->set_leds(
      {fuchsia::input::report::LedType::CAPS_LOCK, fuchsia::input::report::LedType::SCROLL_LOCK});

  fake_device_->SetDescriptor(std::move(descriptor));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Descriptor from file: test\n",
      "Keyboard Descriptor:\n",
      "Input Report:\n",
      "  Key:   458756\n",  // 0x70004
      "  Key:   458834\n",  // 0x70052
      "  Key:   458977\n",  // 0x700e1
      "Output Report:\n",
      "  Led: CAPS_LOCK\n",
      "  Led: SCROLL_LOCK\n",
  });

  print_input_report::PrintInputDescriptor(std::string("test"), &printer, std::move(*client_));
  loop_->RunUntilIdle();
}

TEST_F(PrintInputReport, PrintKeyboardInputReport) {
  fuchsia::input::report::InputReport report;
  report.mutable_keyboard()->set_pressed_keys3(
      {fuchsia::input::Key::A, fuchsia::input::Key::UP, fuchsia::input::Key::LEFT_SHIFT});

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Keyboard Report\n",
      "  Key:   458756\n",  // 0x70004
      "  Key:   458834\n",  // 0x70052
      "  Key:   458977\n",  // 0x700e1
      "\n",
  });

  auto res = print_input_report::GetReaderClient(&client_.value(), loop_->dispatcher());
  ASSERT_EQ(res.status_value(), ZX_OK);
  auto reader = std::move(res.value());

  print_input_report::PrintInputReports("test", &printer, std::move(reader), 1);
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintKeyboardGetInputReport) {
  fuchsia::input::report::InputReport report;
  report.mutable_keyboard()->set_pressed_keys3(
      {fuchsia::input::Key::A, fuchsia::input::Key::UP, fuchsia::input::Key::LEFT_SHIFT});

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Keyboard Report\n",
      "  Key:   458756\n",  // 0x70004
      "  Key:   458834\n",  // 0x70052
      "  Key:   458977\n",  // 0x700e1
      "\n",
  });

  print_input_report::GetAndPrintInputReport(
      "test", fuchsia_input_report::wire::DeviceType::kKeyboard, &printer, std::move(*client_));
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintKeyboardInputReportNoKeys) {
  fuchsia::input::report::InputReport report;
  report.mutable_keyboard()->set_pressed_keys3({});

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "Keyboard Report\n",
      "  No keys pressed\n",
      "\n",
  });

  auto res = print_input_report::GetReaderClient(&client_.value(), loop_->dispatcher());
  ASSERT_EQ(res.status_value(), ZX_OK);
  auto reader = std::move(res.value());

  print_input_report::PrintInputReports("test", &printer, std::move(reader), 1);
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintConsumerControlDescriptor) {
  auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
  descriptor->mutable_consumer_control()->mutable_input()->set_buttons(
      {fuchsia::input::report::ConsumerControlButton::VOLUME_UP,
       fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN,
       fuchsia::input::report::ConsumerControlButton::REBOOT});

  fake_device_->SetDescriptor(std::move(descriptor));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Descriptor from file: test\n",
      "ConsumerControl Descriptor:\n",
      "Input Report:\n",
      "  Button:        VOLUME_UP\n",
      "  Button:      VOLUME_DOWN\n",
      "  Button:           REBOOT\n",
  });

  print_input_report::PrintInputDescriptor(std::string("test"), &printer, std::move(*client_));

  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintConsumerControlReport) {
  fuchsia::input::report::InputReport report;
  report.mutable_consumer_control()->set_pressed_buttons(
      {fuchsia::input::report::ConsumerControlButton::VOLUME_UP,
       fuchsia::input::report::ConsumerControlButton::VOLUME_DOWN,
       fuchsia::input::report::ConsumerControlButton::REBOOT});

  std::vector<fuchsia::input::report::InputReport> reports;
  reports.push_back(std::move(report));
  fake_device_->SetReports(std::move(reports));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Report from file: test\n",
      "ConsumerControl Report\n",
      "  Button:        VOLUME_UP\n",
      "  Button:      VOLUME_DOWN\n",
      "  Button:           REBOOT\n",
      "\n",
  });

  auto res = print_input_report::GetReaderClient(&client_.value(), loop_->dispatcher());
  ASSERT_EQ(res.status_value(), ZX_OK);
  auto reader = std::move(res.value());

  print_input_report::PrintInputReports("test", &printer, std::move(reader), 1);
  loop_->RunUntilIdle();
  printer.AssertSawAllStrings();
}

TEST_F(PrintInputReport, PrintInputDescriptorWithExponents) {
  auto descriptor = std::make_unique<fuchsia::input::report::DeviceDescriptor>();
  auto values = descriptor->mutable_sensor()->mutable_input()->mutable_values();

  fuchsia::input::report::SensorAxis axis;
  axis.axis.unit.type = fuchsia::input::report::UnitType::SI_LINEAR_VELOCITY;
  axis.axis.unit.exponent = -1;
  axis.axis.range.min = 0;
  axis.axis.range.max = 1000;
  axis.type = fuchsia::input::report::SensorType::ACCELEROMETER_X;

  values->push_back(axis);

  axis.axis.unit.type = fuchsia::input::report::UnitType::LUX;
  axis.axis.unit.exponent = -2;
  axis.type = fuchsia::input::report::SensorType::LIGHT_ILLUMINANCE;

  values->push_back(axis);

  fake_device_->SetDescriptor(std::move(descriptor));

  FakePrinter printer;
  printer.SetExpectedStrings(std::vector<std::string>{
      "Descriptor from file: test\n",
      "Sensor Descriptor:\n",
      "  Value 00:\n",
      "    SensorType: ACCELEROMETER_X\n",
      "    Unit: SI_LINEAR_VELOCITY * 1e-1\n",
      "    Min:         0\n",
      "    Max:      1000\n",
      "  Value 01:\n",
      "    SensorType: LIGHT_ILLUMINANCE\n",
      "    Unit:      LUX * 1e-2\n",
      "    Min:         0\n",
      "    Max:      1000\n",
  });

  print_input_report::PrintInputDescriptor(std::string("test"), &printer, std::move(*client_));
  loop_->RunUntilIdle();
}

}  // namespace test
