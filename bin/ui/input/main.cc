// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/usages.h>
#include <iostream>
#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/ui/input/cpp/formatting.h"
#include "lib/ui/input/fidl/input_device_registry.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/log_settings_command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {
int64_t InputEventTimestampNow() {
  return ftl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

}  // namespace

namespace input {
class InputApp {
 public:
  InputApp()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
    registry_ =
        application_context_
            ->ConnectToEnvironmentService<mozart::InputDeviceRegistry>();
  }

  ~InputApp() {}

  void Run(const ftl::CommandLine& command_line) {
    const auto& positional_args = command_line.positional_args();
    if (positional_args.empty()) {
      Usage();
      return;
    }

    uint32_t duration_ms = 0;
    std::string duration_str;
    if (command_line.GetOptionValue("duration", &duration_str)) {
      if (!ftl::StringToNumberWithError(duration_str, &duration_ms)) {
        Error("Invalid duration parameter");
        return;
      }
    }

    if (positional_args[0] == "tap" || positional_args[0] == "swipe") {
      uint32_t width = 1000;
      std::string width_str;
      if (command_line.GetOptionValue("width", &width_str)) {
        if (!ftl::StringToNumberWithError(width_str, &width)) {
          Error("Invalid width parameter");
          return;
        }
      }

      uint32_t height = 1000;
      std::string height_str;
      if (command_line.GetOptionValue("height", &height_str)) {
        if (!ftl::StringToNumberWithError(height_str, &height)) {
          Error("Invalid width height");
          return;
        }
      }

      if (positional_args[0] == "tap") {
        TapEventCommand(positional_args, width, height, duration_ms);
      } else {
        SwipeEventCommand(positional_args, width, height, duration_ms);
      }
    } else if (positional_args[0] == "keyevent") {
      KeyEventCommand(positional_args, duration_ms);
    }
  }

 private:
  void Usage() {
    std::cout << "input keyevent|tap|swipe" << std::endl;
    std::cout << "  keyevent hid_usage (int)" << std::endl;
    std::cout << "  tap x y" << std::endl;
    std::cout << "  swipe x0 y0 x1 y1" << std::endl;
    std::cout << std::endl;

    std::cout << "Options:" << std::endl;
    std::cout
        << "\t--duration=ms to specify the duration of the event (default: 0)."
        << std::endl;

    std::cout << std::endl;
    std::cout << "Swipe and Tap Options:" << std::endl;
    std::cout << std::endl;
    std::cout << "Coordinates will be proportionally converted to the actual "
                 "screen size, but you can specify a virtual range for the "
                 "input."
              << std::endl;
    std::cout
        << "\t--width=w specifies the width of the display (default: 1000)."
        << std::endl;
    std::cout
        << "\t--height=h specifies the height of the display (default: 1000)."
        << std::endl;

    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  void Error(std::string message) {
    std::cout << message << std::endl;
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  }

  mozart::InputDevicePtr RegisterTouchscreen(uint32_t width, uint32_t height) {
    mozart::InputDevicePtr input_device;

    mozart::TouchscreenDescriptorPtr touchscreen =
        mozart::TouchscreenDescriptor::New();
    touchscreen->x = mozart::Axis::New();
    touchscreen->x->range = mozart::Range::New();
    touchscreen->x->range->min = 0;
    touchscreen->x->range->max = width;

    touchscreen->y = mozart::Axis::New();
    touchscreen->y->range = mozart::Range::New();
    touchscreen->y->range->min = 0;
    touchscreen->y->range->max = height;

    mozart::DeviceDescriptorPtr descriptor = mozart::DeviceDescriptor::New();
    descriptor->touchscreen = std::move(touchscreen);

    FTL_VLOG(1) << "Registering " << *descriptor;
    registry_->RegisterDevice(std::move(descriptor), input_device.NewRequest());
    return input_device;
  }

  void TapEventCommand(const std::vector<std::string>& args,
                       uint32_t width,
                       uint32_t height,
                       uint32_t duration_ms) {
    if (args.size() != 3) {
      Usage();
      return;
    }

    int32_t x, y;

    if (!ftl::StringToNumberWithError(args[1], &x)) {
      Error("Invavlid x coordinate");
      return;
    }
    if (!ftl::StringToNumberWithError(args[2], &y)) {
      Error("Invavlid y coordinate");
      return;
    }

    FTL_VLOG(1) << "TapEvent " << x << "x" << y;

    mozart::InputDevicePtr input_device = RegisterTouchscreen(width, height);
    SendTap(std::move(input_device), x, y, duration_ms);
  }

  void KeyEventCommand(const std::vector<std::string>& args,
                       uint32_t duration_ms) {
    if (args.size() != 2) {
      Usage();
      return;
    }

    uint32_t usage;

    if (!ftl::StringToNumberWithError(args[1], &usage)) {
      Error("Invalid HID usage value");
      return;
    }

    if (usage < HID_USAGE_KEY_A || usage > HID_USAGE_KEY_RIGHT_GUI) {
      Error("Invalid HID usage value");
      return;
    }

    FTL_VLOG(1) << "KeyEvent " << usage;

    mozart::KeyboardDescriptorPtr keyboard = mozart::KeyboardDescriptor::New();
    keyboard->keys.resize(HID_USAGE_KEY_RIGHT_GUI - HID_USAGE_KEY_A);
    for (size_t index = HID_USAGE_KEY_A; index < HID_USAGE_KEY_RIGHT_GUI;
         ++index) {
      keyboard->keys[index - HID_USAGE_KEY_A] = index;
    }
    mozart::DeviceDescriptorPtr descriptor = mozart::DeviceDescriptor::New();
    descriptor->keyboard = std::move(keyboard);

    mozart::InputDevicePtr input_device;
    FTL_VLOG(1) << "Registering " << *descriptor;
    registry_->RegisterDevice(std::move(descriptor), input_device.NewRequest());

    SendKeyPress(std::move(input_device), usage, duration_ms);
  }

  void SwipeEventCommand(const std::vector<std::string>& args,
                         uint32_t width,
                         uint32_t height,
                         uint32_t duration) {
    if (args.size() != 5) {
      Usage();
      return;
    }

    int32_t x0, y0, x1, y1;

    if (!ftl::StringToNumberWithError(args[1], &x0)) {
      Error("Invalid x0 coordinate");
      return;
    }
    if (!ftl::StringToNumberWithError(args[2], &y0)) {
      Error("Invalid y0 coordinate");
      return;
    }
    if (!ftl::StringToNumberWithError(args[3], &x1)) {
      Error("Invalid x1 coordinate");
      return;
    }
    if (!ftl::StringToNumberWithError(args[4], &y1)) {
      Error("Invalid y1 coordinate");
      return;
    }

    FTL_VLOG(1) << "SwipeEvent " << x0 << "x" << y0 << " -> " << x1 << "x"
                << y1;
    mozart::InputDevicePtr input_device = RegisterTouchscreen(width, height);

    SendSwipe(std::move(input_device), x0, y0, x1, y1, duration);
  }

  void SendTap(mozart::InputDevicePtr input_device,
               uint32_t x,
               uint32_t y,
               uint32_t duration_ms) {
    // DOWN
    mozart::TouchPtr touch = mozart::Touch::New();
    touch->finger_id = 1;
    touch->x = x;
    touch->y = y;
    mozart::TouchscreenReportPtr touchscreen = mozart::TouchscreenReport::New();
    touchscreen->touches.resize(1);
    touchscreen->touches[0] = std::move(touch);

    mozart::InputReportPtr report = mozart::InputReport::New();
    report->event_time = InputEventTimestampNow();
    report->touchscreen = std::move(touchscreen);

    FTL_VLOG(1) << "SendTap " << *report;
    input_device->DispatchReport(std::move(report));

    ftl::TimeDelta delta = ftl::TimeDelta::FromMilliseconds(duration_ms);
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        ftl::MakeCopyable([device = std::move(input_device)]() mutable {
          // UP
          mozart::TouchscreenReportPtr touchscreen =
              mozart::TouchscreenReport::New();
          touchscreen->touches.resize(0);

          mozart::InputReportPtr report = mozart::InputReport::New();
          report->event_time = InputEventTimestampNow();
          report->touchscreen = std::move(touchscreen);

          FTL_VLOG(1) << "SendTap " << *report;
          device->DispatchReport(std::move(report));
          mtl::MessageLoop::GetCurrent()->PostQuitTask();
        }),
        delta);
  }

  void SendKeyPress(mozart::InputDevicePtr input_device,
                    uint32_t usage,
                    uint32_t duration_ms) {
    // PRESSED
    mozart::KeyboardReportPtr keyboard = mozart::KeyboardReport::New();
    keyboard->pressed_keys.resize(1);
    keyboard->pressed_keys[0] = usage;

    mozart::InputReportPtr report = mozart::InputReport::New();
    report->event_time = InputEventTimestampNow();
    report->keyboard = std::move(keyboard);
    FTL_VLOG(1) << "SendKeyPress " << *report;
    input_device->DispatchReport(std::move(report));

    ftl::TimeDelta delta = ftl::TimeDelta::FromMilliseconds(duration_ms);
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        ftl::MakeCopyable([device = std::move(input_device)]() mutable {

          // RELEASED
          mozart::KeyboardReportPtr keyboard = mozart::KeyboardReport::New();
          keyboard->pressed_keys.resize(0);

          mozart::InputReportPtr report = mozart::InputReport::New();
          report->event_time = InputEventTimestampNow();
          report->keyboard = std::move(keyboard);
          FTL_VLOG(1) << "SendKeyPress " << *report;
          device->DispatchReport(std::move(report));
          mtl::MessageLoop::GetCurrent()->PostQuitTask();
        }),
        delta);
  }

  void SendSwipe(mozart::InputDevicePtr input_device,
                 uint32_t x0,
                 uint32_t y0,
                 uint32_t x1,
                 uint32_t y1,
                 uint32_t duration_ms) {
    // DOWN
    mozart::TouchPtr touch = mozart::Touch::New();
    touch->finger_id = 1;
    touch->x = x0;
    touch->y = y0;
    mozart::TouchscreenReportPtr touchscreen = mozart::TouchscreenReport::New();
    touchscreen->touches.resize(1);
    touchscreen->touches[0] = std::move(touch);

    mozart::InputReportPtr report = mozart::InputReport::New();
    report->event_time = InputEventTimestampNow();
    report->touchscreen = std::move(touchscreen);
    FTL_VLOG(1) << "SendSwipe " << *report;
    input_device->DispatchReport(std::move(report));

    ftl::TimeDelta delta = ftl::TimeDelta::FromMilliseconds(duration_ms);
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        ftl::MakeCopyable(
            [ device = std::move(input_device), x1, y1 ]() mutable {
              // MOVE
              mozart::TouchPtr touch = mozart::Touch::New();
              touch->finger_id = 1;
              touch->x = x1;
              touch->y = y1;
              mozart::TouchscreenReportPtr touchscreen =
                  mozart::TouchscreenReport::New();
              touchscreen->touches.resize(1);
              touchscreen->touches[0] = std::move(touch);

              mozart::InputReportPtr report = mozart::InputReport::New();
              report->event_time = InputEventTimestampNow();
              report->touchscreen = std::move(touchscreen);
              FTL_VLOG(1) << "SendSwipe " << *report;
              device->DispatchReport(std::move(report));

              // UP
              touchscreen = mozart::TouchscreenReport::New();
              touchscreen->touches.resize(0);

              report = mozart::InputReport::New();
              report->event_time = InputEventTimestampNow();
              report->touchscreen = std::move(touchscreen);
              FTL_VLOG(1) << "SendSwipe " << *report;
              device->DispatchReport(std::move(report));

              mtl::MessageLoop::GetCurrent()->PostQuitTask();
            }),
        delta);
  }

  std::unique_ptr<app::ApplicationContext> application_context_;
  fidl::InterfacePtr<mozart::InputDeviceRegistry> registry_;
};
}  // namespace input

int main(int argc, char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;
  input::InputApp app;
  loop.task_runner()->PostTask([&app, command_line] { app.Run(command_line); });
  loop.Run();
  return 0;
}
