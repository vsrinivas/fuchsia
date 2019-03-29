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
#include <trace-provider/provider.h>
#include <trace/event.h>
#include <lib/zx/time.h>

#include "garnet/bin/ui/input/inverse_keymap.h"
#include "lib/component/cpp/connect.h"
#include "lib/component/cpp/startup_context.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/time/time_point.h"
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
    } else if (positional_args[0] == "text") {
      TextCommand(positional_args, duration);
    } else {
      Usage();
    }
  }

 private:
  void Usage() {
    // Keep this up to date with README.md.
    // Until we have standardized usage doc formatting, let's do 100 cols.
    std::cout << R"END(usage: input [<options>] text|keyevent|tap|swipe <args>
  input text <text>
  input keyevent <hid_usage (int)>
  input tap <x> <y>
  input swipe <x0> <y0> <x1> <y1>

global options:
  --duration=<ms>                 the duration of the event, in milliseconds (default: 0)

commands:
  text                            Text is injected by translating to keystrokes using a QWERTY
                                  keymap. Only simple strings are supported; see README.md for
                                  details.

                                  The --duration option is divided over the key events. Care should
                                  be taken not to provide so long a duration that key repeat kicks
                                  in.

                                  Note: when using through fx shell with quotes, you may need to
                                  surround the invocation in strong quotes, e.g.:
                                  fx shell 'input text "Hello, world!"'

  keyevent                        Common usage codes:

                                  key       | code (dec)
                                  ----------|-----
                                  enter     | 40
                                  escape    | 41
                                  backspace | 42
                                  tab       | 43

  tap/swipe                       By default, the x and y coordinates are in the range 0 to 1000
                                  and will be proportionally transformed to the current display,
                                  but you can specify a virtual range for the input with the
                                  --width and --height options.

    options:
      --width=<w>                 the width of the display (default: 1000)
      --height=<h>                the height of the display (default: 1000)

    swipe options:
      --move_event_count=<count>  the number of move events to send in between the down and up
                                  events of the swipe (default: 100)

For further details, see README.md.
)END";

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

  fuchsia::ui::input::InputDevicePtr RegisterKeyboard() {
    fuchsia::ui::input::KeyboardDescriptorPtr keyboard =
        fuchsia::ui::input::KeyboardDescriptor::New();
    keyboard->keys.reserve(HID_USAGE_KEY_RIGHT_GUI - HID_USAGE_KEY_A);
    for (uint32_t usage = HID_USAGE_KEY_A; usage < HID_USAGE_KEY_RIGHT_GUI;
         ++usage) {
      keyboard->keys.push_back(usage);
    }
    fuchsia::ui::input::DeviceDescriptor descriptor;
    descriptor.keyboard = std::move(keyboard);

    fuchsia::ui::input::InputDevicePtr input_device;
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

    fuchsia::ui::input::InputDevicePtr input_device = RegisterKeyboard();
    SendKeyPress(std::move(input_device), usage, duration);
  }

  void TextCommand(const std::vector<std::string>& args,
                   zx::duration duration) {
    if (args.size() != 2) {
      Usage();
      return;
    }

    // TODO(SCN-1068): Default to IME-based input, and have the current mode
    // available as an option.
    // TODO(SCN-1068): Pull default keymap from environment if possible.
    KeySequence key_sequence;
    bool ok;
    std::tie(key_sequence, ok) =
        DeriveKeySequence(InvertKeymap(qwerty_map), args[1]);
    if (!ok) {
      Error("Cannot translate text to key sequence");
      return;
    }

    FXL_VLOG(1) << "Text " << args[1];

    fuchsia::ui::input::InputDevicePtr input_device = RegisterKeyboard();
    SendText(std::move(input_device), std::move(key_sequence), duration);
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
    TRACE_DURATION("input", "SendTap");
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
    TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
    input_device->DispatchReport(std::move(report));

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, device = std::move(input_device)] {
          TRACE_DURATION("input", "SendTap");
          // UP
          fuchsia::ui::input::TouchscreenReportPtr touchscreen =
              fuchsia::ui::input::TouchscreenReport::New();
          touchscreen->touches.resize(0);

          fuchsia::ui::input::InputReport report;
          report.event_time = InputEventTimestampNow();
          report.touchscreen = std::move(touchscreen);

          FXL_VLOG(1) << "SendTap " << report;
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
          device->DispatchReport(std::move(report));
          loop_->Quit();
        },
        duration);
  }

  void SendKeyPress(fuchsia::ui::input::InputDevicePtr input_device,
                    uint32_t usage, zx::duration duration) {
    TRACE_DURATION("input", "SendKeyPress");
    // PRESSED
    fuchsia::ui::input::KeyboardReportPtr keyboard =
        fuchsia::ui::input::KeyboardReport::New();
    keyboard->pressed_keys.push_back(usage);

    fuchsia::ui::input::InputReport report;
    report.event_time = InputEventTimestampNow();
    report.keyboard = std::move(keyboard);
    FXL_VLOG(1) << "SendKeyPress " << report;
    TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
    input_device->DispatchReport(std::move(report));

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, device = std::move(input_device)] {
          TRACE_DURATION("input", "SendKeyPress");
          // RELEASED
          fuchsia::ui::input::KeyboardReportPtr keyboard =
              fuchsia::ui::input::KeyboardReport::New();
          keyboard->pressed_keys.resize(0);

          fuchsia::ui::input::InputReport report;
          report.event_time = InputEventTimestampNow();
          report.keyboard = std::move(keyboard);
          FXL_VLOG(1) << "SendKeyPress " << report;
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
          device->DispatchReport(std::move(report));
          loop_->Quit();
        },
        duration);
  }

  void SendText(fuchsia::ui::input::InputDevicePtr input_device,
                std::vector<fuchsia::ui::input::KeyboardReportPtr> key_sequence,
                zx::duration duration, size_t at = 0) {
    TRACE_DURATION("input", "SendText");
    if (at >= key_sequence.size()) {
      loop_->Quit();
      return;
    }
    fuchsia::ui::input::KeyboardReportPtr keyboard =
        std::move(key_sequence[at]);

    fuchsia::ui::input::InputReport report;
    report.event_time = InputEventTimestampNow();
    report.keyboard = std::move(keyboard);
    FXL_VLOG(1) << "SendText " << report;
    TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
    input_device->DispatchReport(std::move(report));

    zx::duration stroke_duration;
    if (key_sequence.size() > 1) {
      stroke_duration = duration / (key_sequence.size() - 1);
    }

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, device = std::move(input_device),
         key_sequence = std::move(key_sequence), duration, at]() mutable {
          SendText(std::move(device), std::move(key_sequence), duration,
                   at + 1);
        },
        stroke_duration);
    // If the sequence is empty at this point, the next iteration (Quit) is
    // scheduled asap.
  }

  void SendSwipe(fuchsia::ui::input::InputDevicePtr input_device, uint32_t x0,
                 uint32_t y0, uint32_t x1, uint32_t y1, zx::duration duration,
                 uint32_t move_event_count) {
    TRACE_DURATION("input", "SendSwipe");
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
    TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
    input_device->DispatchReport(std::move(report));

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, device = std::move(input_device), x0, y0, x1, y1,
         move_event_count] {
          TRACE_DURATION("input", "SendSwipe");
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
            TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
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
          TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
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
  trace::TraceProvider trace_provider(loop.dispatcher(), "input");
  loop.Run();
  return 0;
}
