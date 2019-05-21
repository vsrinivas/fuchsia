// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <hid/usages.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/input/cpp/formatting.h>
#include <lib/zx/time.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

#include <iostream>
#include <memory>

#include "garnet/bin/ui/input/inverse_keymap.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/lib/fxl/time/time_point.h"

namespace {

int64_t InputEventTimestampNow() {
  return fxl::TimePoint::Now().ToEpochDelta().ToNanoseconds();
}

}  // namespace

namespace input {
class InputApp {
 public:
  InputApp(async::Loop* loop)
      : loop_(loop), component_context_(sys::ComponentContext::Create()) {
    registry_ = component_context_->svc()
                    ->Connect<fuchsia::ui::input::InputDeviceRegistry>();
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

      input_device_ = RegisterTouchscreen(width, height);

      if (positional_args[0] == "tap") {
        uint32_t tap_event_count = 1;
        std::string tap_event_count_str;
        if (command_line.GetOptionValue("tap_event_count",
                                        &tap_event_count_str)) {
          if (!fxl::StringToNumberWithError(tap_event_count_str,
                                            &tap_event_count)) {
            Error("Invalid tap_event_count parameter");
            return;
          }
        }
        TapEventCommand(positional_args, duration, tap_event_count);
      } else {  // "swipe"
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
        SwipeEventCommand(positional_args, duration, move_event_count);
      }
    } else if (positional_args[0] == "keyevent") {
      KeyEventCommand(positional_args, duration);
    } else if (positional_args[0] == "text") {
      TextCommand(positional_args, duration);
    } else if (positional_args[0] == "media_button") {
      MediaButtonEventCommand(positional_args);
    } else {
      Usage();
    }
  }

 private:
  void Usage() {
    // Keep this up to date with README.md.
    // Until we have standardized usage doc formatting, let's do 100 cols.
    std::cout
        << R"END(usage: input [<options>] text|keyevent|tap|swipe|media_button <args>
  input text <text>
  input keyevent <hid_usage (int)>
  input tap <x> <y>
  input swipe <x0> <y0> <x1> <y1>
  input media_button <mic_mute> <volume_up> <volume_down> <reset>

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

  media_button                    Sends a MediaButton event. All fields are booleans and must
                                  be either 0 or 1.

    options:
      --width=<w>                 the width of the display (default: 1000)
      --height=<h>                the height of the display (default: 1000)

    swipe options:
      --move_event_count=<count>  the number of move events to send in between the down and up
                                  events of the swipe (default: 100)

      --tap_event_count=<count>   the number of tap events to send (default: 1)
                                  The --duration option is divided over the tap events.

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

  fuchsia::ui::input::InputDevicePtr RegisterMediaButtons() {
    fuchsia::ui::input::MediaButtonsDescriptorPtr mediabuttons =
        fuchsia::ui::input::MediaButtonsDescriptor::New();
    mediabuttons->buttons = fuchsia::ui::input::kMicMute |
                            fuchsia::ui::input::kVolumeUp |
                            fuchsia::ui::input::kVolumeDown;
    fuchsia::ui::input::DeviceDescriptor descriptor;
    descriptor.media_buttons = std::move(mediabuttons);

    fuchsia::ui::input::InputDevicePtr input_device;
    FXL_VLOG(1) << "Registering " << descriptor;
    registry_->RegisterDevice(std::move(descriptor), input_device.NewRequest());
    return input_device;
  }

  void MediaButtonEventCommand(const std::vector<std::string>& args) {
    if (args.size() != 5) {
      Usage();
      return;
    }

    int32_t mic_mute, volume_up, volume_down, reset;
    if (!fxl::StringToNumberWithError(args[1], &mic_mute)) {
      Error("Invalid mic_mute number");
      return;
    }
    if (mic_mute != 0 && mic_mute != 1) {
      Error("mic_mute must be 0 or 1");
      return;
    }
    if (!fxl::StringToNumberWithError(args[2], &volume_up)) {
      Error("Invalid volume_up number");
      return;
    }
    if (volume_up < 0 || volume_up > 1) {
      Error("volume_up must be 0 or 1");
      return;
    }
    if (!fxl::StringToNumberWithError(args[3], &volume_down)) {
      Error("Invalid volume_down number");
      return;
    }
    if (volume_down < 0 || volume_down > 1) {
      Error("volume_down must be 0 or 1");
      return;
    }
    if (!fxl::StringToNumberWithError(args[4], &reset)) {
      Error("Invalid reset number");
      return;
    }
    if (reset < 0 || reset > 1) {
      Error("reset must be 0 or 1");
      return;
    }

    fuchsia::ui::input::InputDevicePtr input_device = RegisterMediaButtons();
    SendMediaButton(std::move(input_device), mic_mute, volume_up, volume_down,
                    reset);
  }

  void SendMediaButton(fuchsia::ui::input::InputDevicePtr input_device,
                       bool mic_mute, bool volume_up, bool volume_down,
                       bool reset) {
    TRACE_DURATION("input", "SendMediaButton");
    fuchsia::ui::input::MediaButtonsReportPtr media_buttons =
        fuchsia::ui::input::MediaButtonsReport::New();
    media_buttons->mic_mute = mic_mute;
    media_buttons->volume_up = volume_up;
    media_buttons->volume_down = volume_down;
    media_buttons->reset = reset;

    fuchsia::ui::input::InputReport report;
    report.event_time = InputEventTimestampNow();
    report.media_buttons = std::move(media_buttons);

    FXL_VLOG(1) << "SendMediaButton " << report;
    TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
    input_device->DispatchReport(std::move(report));
    loop_->Quit();
  }

  void TapEventCommand(const std::vector<std::string>& args,
                       zx::duration duration, uint32_t tap_event_count) {
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

    zx::duration tap_duration = duration;
    if (tap_event_count > 1)
      tap_duration = duration / tap_event_count;

    SendTap(x, y, tap_duration, tap_event_count, 0);
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

  void SwipeEventCommand(const std::vector<std::string>& args,
                         zx::duration duration, uint32_t move_event_count) {
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

    SendSwipe(x0, y0, x1, y1, duration, move_event_count);
  }

  void SendTap(int32_t x, int32_t y, zx::duration tap_duration,
               uint32_t max_tap_count, uint32_t cur_tap_count) {
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
    input_device_->DispatchReport(std::move(report));

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, x, y, tap_duration, max_tap_count, cur_tap_count]() mutable {
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
          input_device_->DispatchReport(std::move(report));
          if (++cur_tap_count >= max_tap_count) {
            loop_->Quit();
            return;
          }
          SendTap(x, y, tap_duration, max_tap_count, cur_tap_count);
        },
        tap_duration);
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

  void SendSwipe(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                 zx::duration duration, uint32_t move_event_count) {
    TRACE_DURATION("input", "SendSwipe");

    zx::duration swipe_event_delay = duration;
    float deltaX = (x1 - x0);
    float deltaY = (y1 - y0);
    if (move_event_count > 1) {
      // We have move_event_count + 2 events:
      //   DOWN
      //   MOVE x move_event_count
      //   UP
      // so we need (move_event_count + 1) delays.
      swipe_event_delay = duration / (move_event_count + 1);
      deltaX = deltaX / move_event_count;
      deltaY = deltaY / move_event_count;
    }

    // DOWN
    SendTouchEvent(x0, y0);

    for (int32_t i = 1; i <= (int32_t)move_event_count; i++) {
      // MOVE
      async::PostDelayedTask(
          async_get_default_dispatcher(),
          [this, i, x0, y0, deltaX, deltaY] {
            SendTouchEvent(x0 + round(i * deltaX), y0 + round(i * deltaY));
          },
          swipe_event_delay * i);
    }

    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this] {
          TRACE_DURATION("input", "SendSwipe");

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
          input_device_->DispatchReport(std::move(report));

          loop_->Quit();
        },
        duration);
  }

  void SendTouchEvent(int32_t x, int32_t y) {
    TRACE_DURATION("input", "SendSwipe");
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
    FXL_VLOG(1) << "SendSwipe " << report;
    TRACE_FLOW_BEGIN("input", "hid_read_to_listener", report.trace_id);
    input_device_->DispatchReport(std::move(report));
  }

  async::Loop* const loop_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  fidl::InterfacePtr<fuchsia::ui::input::InputDeviceRegistry> registry_;
  fuchsia::ui::input::InputDevicePtr input_device_;
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
