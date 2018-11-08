// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/usages.h>
#include <iostream>
#include <memory>

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <zx/time.h>

#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/time/time_point.h"
#include "lib/ui/input/cpp/formatting.h"

namespace {

int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

}  // namespace

namespace input {
class InputApp {
 public:
  InputApp(async::Loop* loop)
      : loop_(loop),
        startup_context_(component::StartupContext::CreateFromStartupInfo()) {
    registry_ = startup_context_->ConnectToEnvironmentService<
        fuchsia::ui::input::InputDeviceRegistry>();
  }

  ~InputApp() {}

  void Run(const fxl::CommandLine& command_line) {
    const auto& positional_args = command_line.positional_args();
    if (positional_args.empty()) {
      Usage();
      return;
    }

    zx::duration duration;

    {
      std::string duration_str;
      if (command_line.GetOptionValue("duration", &duration_str)) {
        uint32_t duration_ms;
        if (!fxl::StringToNumberWithError(duration_str, &duration_ms)) {
          Error("Invalid duration parameter");
          return;
        }
        duration = zx::msec(duration_ms);
      }
    }

    if (positional_args[0] == "tap" || positional_args[0] == "swipe") {
      uint32_t width = 1000;
      std::string width_str;
      if (command_line.GetOptionValue("width", &width_str)) {
        if (!fxl::StringToNumberWithError(width_str, &width)) {
          Error("Invalid width parameter");
          return;
        }
      }

      uint32_t height = 1000;
      std::string height_str;
      if (command_line.GetOptionValue("height", &height_str)) {
        if (!fxl::StringToNumberWithError(height_str, &height)) {
          Error("Invalid height parameter");
          return;
        }
      }

      if (positional_args[0] == "tap") {
        TapEventCommand(positional_args, width, height, duration);
      } else {
        uint32_t move_event_count = 100;
        std::string move_event_count_str;
        if (command_line.GetOptionValue("move_event_count",
                                        &move_event_count_str)) {
          if (!fxl::StringToNumberWithError(move_event_count_str,
                                            &move_event_count)) {
            Error("Invalid move_event_count parameter");
            return;
          }
        }
        SwipeEventCommand(positional_args, width, height, duration,
                          move_event_count);
      }
    } else if (positional_args[0] == "keyevent") {
      KeyEventCommand(positional_args, duration);
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

    std::cout << std::endl;
    std::cout << "Swipe Options:" << std::endl;
    std::cout
        << "\t--move_event_count=count specifies the amount of move events to "
           "send in between the up and down events of the swipe (default: 100)"
        << std::endl;

    loop_->Quit();
  }

  void Error(std::string message) {
    std::cout << message << std::endl;
    loop_->Quit();
  }

  fuchsia::ui::input::InputDevicePtr RegisterTouchscreen(uint32_t width,
                                                         uint32_t height) {
    fuchsia::ui::input::InputDevicePtr input_device;

    fuchsia::ui::input::TouchscreenDescriptorPtr touchscreen =
        fuchsia::ui::input::TouchscreenDescriptor::New();
    touchscreen->x.range.min = 0;
    touchscreen->x.range.max = width;

    touchscreen->y.range.min = 0;
    touchscreen->y.range.max = height;

    fuchsia::ui::input::DeviceDescriptor descriptor;
    descriptor.touchscreen = std::move(touchscreen);

    FXL_VLOG(1) << "Registering " << descriptor;
    registry_->RegisterDevice(std::move(descriptor), input_device.NewRequest());
    return input_device;
  }

  void TapEventCommand(const std::vector<std::string>& args, uint32_t width,
                       uint32_t height, zx::duration duration) {
    if (args.size() != 3) {
      Usage();
      return;
    }

    int32_t x, y;

    if (!fxl::StringToNumberWithError(args[1], &x)) {
      Error("Invalid x coordinate");
      return;
    }
    if (!fxl::StringToNumberWithError(args[2], &y)) {
      Error("Invalid y coordinate");
      return;
    }

    FXL_VLOG(1) << "TapEvent " << x << "x" << y;

    fuchsia::ui::input::InputDevicePtr input_device =
        RegisterTouchscreen(width, height);
    SendTap(std::move(input_device), x, y, duration);
  }

  void KeyEventCommand(const std::vector<std::string>& args,
                       zx::duration duration) {
    if (args.size() != 2) {
      Usage();
      return;
    }

    uint32_t usage;

    if (!fxl::StringToNumberWithError(args[1], &usage)) {
      Error("Invalid HID usage value");
      return;
    }

    if (usage < HID_USAGE_KEY_A || usage > HID_USAGE_KEY_RIGHT_GUI) {
      Error("Invalid HID usage value");
      return;
    }

    FXL_VLOG(1) << "KeyEvent " << usage;

    fuchsia::ui::input::KeyboardDescriptorPtr keyboard =
        fuchsia::ui::input::KeyboardDescriptor::New();
    keyboard->keys.resize(HID_USAGE_KEY_RIGHT_GUI - HID_USAGE_KEY_A);
    for (size_t index = HID_USAGE_KEY_A; index < HID_USAGE_KEY_RIGHT_GUI;
         ++index) {
      keyboard->keys->at(index - HID_USAGE_KEY_A) = index;
    }
    fuchsia::ui::input::DeviceDescriptor descriptor;
    descriptor.keyboard = std::move(keyboard);

    fuchsia::ui::input::InputDevicePtr input_device;
    FXL_VLOG(1) << "Registering " << descriptor;
    registry_->RegisterDevice(std::move(descriptor), input_device.NewRequest());

    SendKeyPress(std::move(input_device), usage, duration);
  }

  void SwipeEventCommand(const std::vector<std::string>& args, uint32_t width,
                         uint32_t height, zx::duration duration,
                         uint32_t move_event_count) {
    if (args.size() != 5) {
      Usage();
      return;
    }

    int32_t x0, y0, x1, y1;

    if (!fxl::StringToNumberWithError(args[1], &x0)) {
      Error("Invalid x0 coordinate");
      return;
    }
    if (!fxl::StringToNumberWithError(args[2], &y0)) {
      Error("Invalid y0 coordinate");
      return;
    }
    if (!fxl::StringToNumberWithError(args[3], &x1)) {
      Error("Invalid x1 coordinate");
      return;
    }
    if (!fxl::StringToNumberWithError(args[4], &y1)) {
      Error("Invalid y1 coordinate");
      return;
    }

    FXL_VLOG(1) << "SwipeEvent " << x0 << "x" << y0 << " -> " << x1 << "x"
                << y1;
    fuchsia::ui::input::InputDevicePtr input_device =
        RegisterTouchscreen(width, height);

    SendSwipe(std::move(input_device), x0, y0, x1, y1, duration,
              move_event_count);
  }

  void SendTap(fuchsia::ui::input::InputDevicePtr input_device, uint32_t x,
               uint32_t y, zx::duration duration) {
    // DOWN
    fuchsia::ui::input::Touch touch;
    touch.finger_id = 1;
    touch.x = x;
    touch.y = y;
    fuchsia::ui::input::TouchscreenReportPtr touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();
    touchscreen->touches.push_back(std::move(touch));

    fuchsia::ui::input::InputReport report;
    report.event_time = InputEventTimestampNow();
    report.touchscreen = std::move(touchscreen);

    FXL_VLOG(1) << "SendTap " << report;
    input_device->DispatchReport(std::move(report));

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, device = std::move(input_device)] {
          // UP
          fuchsia::ui::input::TouchscreenReportPtr touchscreen =
              fuchsia::ui::input::TouchscreenReport::New();
          touchscreen->touches.resize(0);

          fuchsia::ui::input::InputReport report;
          report.event_time = InputEventTimestampNow();
          report.touchscreen = std::move(touchscreen);

          FXL_VLOG(1) << "SendTap " << report;
          device->DispatchReport(std::move(report));
          loop_->Quit();
        },
        duration);
  }

  void SendKeyPress(fuchsia::ui::input::InputDevicePtr input_device,
                    uint32_t usage, zx::duration duration) {
    // PRESSED
    fuchsia::ui::input::KeyboardReportPtr keyboard =
        fuchsia::ui::input::KeyboardReport::New();
    keyboard->pressed_keys.push_back(usage);

    fuchsia::ui::input::InputReport report;
    report.event_time = InputEventTimestampNow();
    report.keyboard = std::move(keyboard);
    FXL_VLOG(1) << "SendKeyPress " << report;
    input_device->DispatchReport(std::move(report));

    async::PostDelayedTask(async_get_default_dispatcher(),
                           [this, device = std::move(input_device)] {
                             // RELEASED
                             fuchsia::ui::input::KeyboardReportPtr keyboard =
                                 fuchsia::ui::input::KeyboardReport::New();
                             keyboard->pressed_keys.resize(0);

                             fuchsia::ui::input::InputReport report;
                             report.event_time = InputEventTimestampNow();
                             report.keyboard = std::move(keyboard);
                             FXL_VLOG(1) << "SendKeyPress " << report;
                             device->DispatchReport(std::move(report));
                             loop_->Quit();
                           },
                           duration);
  }

  void SendSwipe(fuchsia::ui::input::InputDevicePtr input_device, uint32_t x0,
                 uint32_t y0, uint32_t x1, uint32_t y1, zx::duration duration,
                 uint32_t move_event_count) {
    // DOWN
    fuchsia::ui::input::Touch touch;
    touch.finger_id = 1;
    touch.x = x0;
    touch.y = y0;
    fuchsia::ui::input::TouchscreenReportPtr touchscreen =
        fuchsia::ui::input::TouchscreenReport::New();
    touchscreen->touches.push_back(std::move(touch));

    fuchsia::ui::input::InputReport report;
    report.event_time = InputEventTimestampNow();
    report.touchscreen = std::move(touchscreen);
    FXL_VLOG(1) << "SendSwipe " << report;
    input_device->DispatchReport(std::move(report));

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, device = std::move(input_device), x0, y0, x1, y1,
         move_event_count] {
          // MOVE
          for (uint32_t i = 0; i < move_event_count; i++) {
            fuchsia::ui::input::Touch touch;
            touch.finger_id = 1;

            auto blend = [](float a, float b, float factor) {
              return a * (1.0f - factor) + b * factor;
            };
            float factor = float(i) / float(move_event_count);
            touch.x = blend(x0, x1, factor);
            touch.y = blend(y0, y1, factor);

            fuchsia::ui::input::TouchscreenReportPtr touchscreen =
                fuchsia::ui::input::TouchscreenReport::New();
            touchscreen->touches.push_back(std::move(touch));

            fuchsia::ui::input::InputReport report;
            report.event_time = InputEventTimestampNow();
            report.touchscreen = std::move(touchscreen);
            FXL_VLOG(1) << "SendSwipe " << report;
            device->DispatchReport(std::move(report));
          }

          // UP
          fuchsia::ui::input::TouchscreenReportPtr touchscreen =
              fuchsia::ui::input::TouchscreenReport::New();
          touchscreen->touches.resize(0);

          fuchsia::ui::input::InputReport report =
              fuchsia::ui::input::InputReport();
          report.event_time = InputEventTimestampNow();
          report.touchscreen = std::move(touchscreen);
          FXL_VLOG(1) << "SendSwipe " << report;
          device->DispatchReport(std::move(report));

          loop_->Quit();
        },
        duration);
  }

  async::Loop* const loop_;
  std::unique_ptr<component::StartupContext> startup_context_;
  fidl::InterfacePtr<fuchsia::ui::input::InputDeviceRegistry> registry_;
};
}  // namespace input

int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  input::InputApp app(&loop);
  async::PostTask(loop.dispatcher(),
                  [&app, command_line] { app.Run(command_line); });
  loop.Run();
  return 0;
}
