// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <cstdio>
#include <deque>
#include <iomanip>
#include <iostream>

#include <fuchsia/media/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/command_line.h"
#include "lib/media/audio/perceived_level.h"

namespace media {
namespace {

static constexpr int kLevelMax = 25;
static constexpr char kClearEol[] = "\x1b[K";
static constexpr char kHideCursor[] = "\x1b[?25l";
static constexpr char kShowCursor[] = "\x1b[?25h";

}  // namespace

using AudioGainInfo = ::fuchsia::media::AudioGainInfo;
using AudioDeviceInfo = ::fuchsia::media::AudioDeviceInfo;

class EscapeDecoder {
 public:
  static constexpr int kUpArrow = -10;
  static constexpr int kDownArrow = -11;
  static constexpr int kRightArrow = -12;
  static constexpr int kLeftArrow = -13;

  EscapeDecoder() = default;
  EscapeDecoder(const EscapeDecoder&) = delete;
  EscapeDecoder(EscapeDecoder&&) = delete;
  EscapeDecoder& operator=(const EscapeDecoder&) = delete;
  EscapeDecoder& operator=(EscapeDecoder&&);

  int Decode(int c) {
    if (state_ == 2) {
      state_ = 0;
      // clang-format off
      switch (c) {
        case 'A': return kUpArrow;
        case 'B': return kDownArrow;
        case 'C': return kRightArrow;
        case 'D': return kLeftArrow;
        default: return 0;
      }
      // clang-format on
    }

    if (state_ == 1) {
      state_ = (c == kBracketChar) ? 2 : 0;
      return 0;
    }

    if (c == kEscChar) {
      state_ = 1;
      return 0;
    }

    return c;
  }

 private:
  static constexpr int kEscChar = 0x1b;
  static constexpr int kBracketChar = '[';
  uint32_t state_ = 0;
};

class VolApp {
 public:
  VolApp(int argc, const char** argv, fit::closure quit_callback)
      : startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
        quit_callback_(std::move(quit_callback)) {
    FXL_DCHECK(quit_callback_);

    fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

    if (command_line.HasOption("help")) {
      Usage();
      return;
    }

    bool uid_set = false;
    bool token_set = false;
    std::string string_value;
    if (command_line.GetOptionValue("uid", &string_value)) {
      if (!string_value.length()) {
        Usage();
        return;
      }

      selected_uid_ = string_value;
      uid_set = true;
    }

    if (command_line.GetOptionValue("token", &string_value)) {
      if (uid_set || !Parse(string_value, &selected_token_) ||
          (selected_token_ == ZX_KOID_INVALID)) {
        Usage();
        return;
      }

      token_set = true;
    }

    if (command_line.HasOption("input")) {
      if (uid_set || token_set) {
        Usage();
        return;
      }
      input_ = true;
    }

    if (command_line.HasOption("show")) {
      non_interactive_actions_.emplace_back([this]() { ShowAllDevices(); });
    }

    if (command_line.GetOptionValue("mute", &string_value)) {
      BoolAction val;

      if (!Parse(string_value, &val)) {
        Usage();
        return;
      }

      non_interactive_actions_.emplace_back(
          [this, val]() { SetDeviceMute(val); });
    }

    if (command_line.GetOptionValue("agc", &string_value)) {
      BoolAction val;

      if (!Parse(string_value, &val)) {
        Usage();
        return;
      }

      non_interactive_actions_.emplace_back(
          [this, val]() { SetDeviceAgc(val); });
    }

    if (command_line.GetOptionValue("gain", &string_value)) {
      float val;

      if (!Parse(string_value, &val)) {
        Usage();
        return;
      }

      non_interactive_actions_.emplace_back(
          [this, val]() { SetDeviceGain(val, false); });
    }

    audio_ = startup_context_->ConnectToEnvironmentService<
        fuchsia::media::AudioDeviceEnumerator>();
    audio_.set_error_handler([this]() {
      std::cout << "System error: audio service failure";
      quit_callback_();
    });

    // Get this party started by fetching the current list of audio devices.
    audio_->GetDevices([this](std::vector<AudioDeviceInfo> devices) {
      OnGetDevices(std::move(devices));
    });
  }

 private:
  enum class BoolAction {
    kTrue,
    kFalse,
    kToggle,
  };

  void InteractiveUsage() {
    std::cout << "\ninteractive mode:\n";
    std::cout << "    +            increase system gain\n";
    std::cout << "    -            decrease system gain\n";
    std::cout << "    m            toggle mute\n";
    std::cout << "    a            toggle agc\n";
    std::cout << "    enter        quit\n";
  }

  void Usage() {
    std::cout << "\n";
    std::cout << "vol <args>\n";
    std::cout << "    --show           show system audio status\n";
    std::cout << "    --token=<id>     select the device by token\n";
    std::cout << "    --uid=<uid>      select the device by partial UID\n";
    std::cout << "    --input          select the default input device\n";
    std::cout << "    --gain=<db>      set system audio gain\n";
    std::cout << "    --mute=(on|off)  mute/unmute system audio\n";
    std::cout << "    --agc=(on|off)   enable/disable AGC\n\n";
    std::cout << "Given no arguments, vol waits for the following keystrokes\n";
    InteractiveUsage();
    std::cout << "\n";

    quit_callback_();
  }

  bool Parse(const std::string& string_value, float* float_out) {
    FXL_DCHECK(float_out);

    std::istringstream istream(string_value);
    return (istream >> *float_out) && istream.eof();
  }

  bool Parse(const std::string& string_value, uint64_t* uint_out) {
    FXL_DCHECK(uint_out);

    std::istringstream istream(string_value);
    return (istream >> *uint_out) && istream.eof();
  }

  bool Parse(const std::string& string_value, BoolAction* bool_out) {
    FXL_DCHECK(bool_out);

    static const char* TRUE_STRINGS[] = {"yes", "on", "true"};
    for (const char* s : TRUE_STRINGS) {
      if (!strcasecmp(string_value.c_str(), s)) {
        *bool_out = BoolAction::kTrue;
        return true;
      }
    }

    static const char* FALSE_STRINGS[] = {"no", "off", "false"};
    for (const char* s : FALSE_STRINGS) {
      if (!strcasecmp(string_value.c_str(), s)) {
        *bool_out = BoolAction::kFalse;
        return true;
      }
    }

    return false;
  }

  void FormatGainMute(std::ostream& os, const AudioGainInfo& info) {
    int level = PerceivedLevel::GainToLevel(info.db_gain, kLevelMax);

    namespace flag = ::fuchsia::media;
    bool muted = (info.flags & flag::AudioGainInfoFlag_Mute) != 0;
    bool can_agc = (info.flags & flag::AudioGainInfoFlag_AgcSupported) != 0;
    bool agc = (info.flags & flag::AudioGainInfoFlag_AgcEnabled) != 0;

    os << std::string(level, '=') << "|" << std::string(kLevelMax - level, '-')
       << " :: [" << (muted ? " muted " : "unmuted") << "]"
       << (can_agc ? (agc ? "[agc]" : "[   ]") : "") << " " << std::fixed
       << std::setprecision(2) << info.db_gain << " dB";
  }

  // Calls |HandleKeystroke| on the message loop when console input is ready.
  void WaitForKeystroke() {
    fd_waiter_.Wait(
        [this](zx_status_t status, uint32_t events) { HandleKeystroke(); }, 0,
        POLLIN);
  }

  // Handles a keystroke, possibly calling |WaitForKeystroke| to wait for the
  // next one.
  void HandleKeystroke() {
    int c = esc_decoder_.Decode(getc(stdin));

    switch (c) {
      case '+':
      case EscapeDecoder::kUpArrow:
      case EscapeDecoder::kRightArrow:
        SetDeviceGain(1.0, true);
        break;
      case '-':
      case EscapeDecoder::kDownArrow:
      case EscapeDecoder::kLeftArrow:
        SetDeviceGain(-1.0, true);
        break;
      case 'a':
      case 'A':
        SetDeviceAgc(BoolAction::kToggle);
        break;
      case 'm':
      case 'M':
        SetDeviceMute(BoolAction::kToggle);
        break;
      case '\n':
      case '\r':
      case 'q':
      case 'Q':
        quit_callback_();
        std::cout << kShowCursor << "\n" << std::endl;
        return;
      default:
        break;
    }

    WaitForKeystroke();
  }

  void ShowAllDevices() {
    for (const auto& map_entry : devices_) {
      const auto& dev = map_entry.second;
      namespace flag = ::fuchsia::media;

      bool muted = (dev.gain_info.flags & flag::AudioGainInfoFlag_Mute) != 0;
      bool can_agc =
          (dev.gain_info.flags & flag::AudioGainInfoFlag_AgcSupported) != 0;
      bool agc_enb =
          (dev.gain_info.flags & flag::AudioGainInfoFlag_AgcEnabled) != 0;

      std::cout << "Audio " << (dev.is_input ? "Input" : "Output") << " (id "
                << dev.token_id << ")" << std::endl;
      std::cout << "Name    : " << dev.name << std::endl;
      std::cout << "UID     : " << dev.unique_id << std::endl;
      std::cout << "Default : " << (dev.is_default ? "yes" : "no") << std::endl;
      std::cout << "Gain    : " << dev.gain_info.db_gain << " dB" << std::endl;
      std::cout << "Mute    : " << (muted ? "yes" : "no") << std::endl;
      if (can_agc) {
        std::cout << "AGC     : " << (agc_enb ? "yes" : "no") << std::endl;
      }
    }
  }

  void SetDeviceGain(float val, bool relative) {
    auto iter = devices_.find(control_token_);

    if (iter == devices_.end()) {
      if (!interactive()) {
        std::cout << "No appropriate device found for setting gain";
      }
      return;
    }

    const auto& dev_state = devices_[control_token_];
    AudioGainInfo cmd = dev_state.gain_info;
    cmd.db_gain = relative ? (cmd.db_gain + val) : val;

    if (!interactive()) {
      std::cout << "Setting audio " << (dev_state.is_input ? "input" : "output")
                << " \"" << dev_state.name << "\" gain to "
                << std::setprecision(2) << cmd.db_gain << " dB" << std::endl;
    }

    audio_->SetDeviceGain(control_token_, std::move(cmd),
                          ::fuchsia::media::SetAudioGainFlag_GainValid);
  }

  void SetDeviceMute(BoolAction action) {
    auto iter = devices_.find(control_token_);

    if (iter == devices_.end()) {
      if (!interactive()) {
        std::cout << "No appropriate device found for setting mute"
                  << std::endl;
      }
      return;
    }

    const auto& dev_state = devices_[control_token_];
    AudioGainInfo cmd = dev_state.gain_info;

    constexpr uint32_t flag = ::fuchsia::media::AudioGainInfoFlag_Mute;
    // clang-format off
    switch (action) {
      case BoolAction::kTrue: cmd.flags |= flag; break;
      case BoolAction::kFalse: cmd.flags &= ~flag; break;
      case BoolAction::kToggle: cmd.flags ^= flag; break;
    }
    // clang-format on

    if (!interactive()) {
      std::cout << "Setting audio " << (dev_state.is_input ? "input" : "output")
                << " \"" << dev_state.name << "\" mute to "
                << ((cmd.flags & flag) ? "on" : "off") << "." << std::endl;
    }

    audio_->SetDeviceGain(control_token_, std::move(cmd),
                          ::fuchsia::media::SetAudioGainFlag_MuteValid);
  }

  void SetDeviceAgc(BoolAction action) {
    auto iter = devices_.find(control_token_);

    if (iter == devices_.end()) {
      if (!interactive()) {
        std::cout << "No appropriate device found for setting agc" << std::endl;
      }
      return;
    }

    const auto& dev_state = devices_[control_token_];
    AudioGainInfo cmd = dev_state.gain_info;

    if (!(cmd.flags & ::fuchsia::media::AudioGainInfoFlag_AgcSupported)) {
      if (!interactive()) {
        std::cout << "Audio " << (dev_state.is_input ? "input" : "output")
                  << " \"" << dev_state.name << "\" does not support AGC."
                  << std::endl;
      }
      return;
    }

    constexpr uint32_t flag = ::fuchsia::media::AudioGainInfoFlag_AgcEnabled;
    // clang-format off
    switch (action) {
      case BoolAction::kTrue: cmd.flags |= flag; break;
      case BoolAction::kFalse: cmd.flags &= ~flag; break;
      case BoolAction::kToggle: cmd.flags ^= flag; break;
    }
    // clang-format on

    if (!interactive()) {
      std::cout << "Setting audio " << (dev_state.is_input ? "input" : "output")
                << " \"" << dev_state.name << "\" AGC to "
                << ((cmd.flags & flag) ? "on" : "off") << "." << std::endl;
    }

    audio_->SetDeviceGain(control_token_, std::move(cmd),
                          ::fuchsia::media::SetAudioGainFlag_AgcValid);
  }

  void ShowSelectedDevice() {
    if (control_token_ != ZX_KOID_INVALID) {
      const auto& dev = devices_[control_token_];
      std::cout << "\rCurrently controlling audio "
                << (input_ ? "input" : "output") << " (id " << dev.token_id
                << "): " << dev.name << std::endl;
    } else {
      std::cout << "\rNo appropriate audio " << (input_ ? "input" : "output")
                << " exists to control" << std::endl;
    }

    std::cout << kClearEol << std::flush;
  }

  void RedrawInteractiveState() {
    std::cout << "\r";
    if (control_token_ != ZX_KOID_INVALID) {
      FormatGainMute(std::cout, devices_[control_token_].gain_info);
    } else {
      std::cout << "No device selected!";
    }
    std::cout << kClearEol << std::flush;
  }

  template <typename T>
  bool ChooseDeviceToControl(const T& predicate) {
    uint64_t token = ZX_KOID_INVALID;
    uint64_t prev_token = control_token_;

    for (const auto& pair : devices_) {
      const auto& dev = pair.second;

      if (predicate(dev)) {
        token = dev.token_id;
        break;
      }
    }

    control_token_ = token;
    return prev_token != control_token_;
  }

  bool ChooseDeviceToControl() {
    if (selected_uid_.length()) {
      return ChooseDeviceToControl([uid_ptr = selected_uid_.c_str(),
                                    uid_len = selected_uid_.length()](
                                       const AudioDeviceInfo& info) -> bool {
        return (strncmp(info.unique_id.get().c_str(), uid_ptr, uid_len) == 0);
      });
    } else if (selected_token_ != ZX_KOID_INVALID) {
      return ChooseDeviceToControl(
          [token = selected_token_](const AudioDeviceInfo& info) -> bool {
            return info.token_id == token;
          });
    } else {
      return ChooseDeviceToControl(
          [input = input_](const AudioDeviceInfo& info) -> bool {
            return (info.is_input == input) && info.is_default;
          });
    }
  }

  void OnGetDevices(std::vector<AudioDeviceInfo> devices) {
    // Build our device map.
    for (auto& dev : devices) {
      auto result =
          devices_.emplace(std::make_pair(dev.token_id, std::move(dev)));
      if (!result.second) {
        std::cerr << "<WARNING>: Duplicate audio device token ID ("
                  << dev.token_id << std::endl;
        continue;
      }
    }

    // Choose the device we want to control.
    ChooseDeviceToControl();

    if (!interactive()) {
      // Take the actions requested by the user.
      for (const auto& action : non_interactive_actions_) {
        action();
      }

      // Then exit.
      quit_callback_();
    } else {
      InteractiveUsage();
      std::cout << "\n" << kHideCursor;

      // Install our event hooks so we can keep up with any changes to our
      // device state.
      audio_.events().OnDeviceAdded = [this](AudioDeviceInfo dev) {
        OnDeviceAdded(std::move(dev));
      };
      audio_.events().OnDeviceRemoved = [this](uint64_t dev_token) {
        OnDeviceRemoved(dev_token);
      };
      audio_.events().OnDeviceGainChanged = [this](uint64_t dev_token,
                                                   AudioGainInfo info) {
        OnDeviceGainChanged(dev_token, info);
      };
      audio_.events().OnDefaultDeviceChanged = [this](uint64_t old_id,
                                                      uint64_t new_id) {
        OnDefaultDeviceChanged(old_id, new_id);
      };

      setbuf(stdin, nullptr);
      WaitForKeystroke();

      ShowSelectedDevice();
      RedrawInteractiveState();
    }
  }

  void OnDeviceAdded(AudioDeviceInfo device_to_add, bool skip_update = false) {
    uint64_t token = device_to_add.token_id;
    auto result =
        devices_.emplace(std::make_pair(token, std::move(device_to_add)));

    if (!result.second) {
      std::cerr << "\r<WARNING>: Duplicate audio device token ID (" << token
                << ")" << std::endl;
      return;
    }

    if (!skip_update) {
      if (ChooseDeviceToControl()) {
        ShowSelectedDevice();
        RedrawInteractiveState();
      }
    }
  }

  void OnDeviceRemoved(uint64_t dev_token) {
    auto iter = devices_.find(dev_token);
    if (iter == devices_.end()) {
      std::cerr << "\r<WARNING>: Invalid device token (" << dev_token
                << ") during device remove notification." << std::endl;
      return;
    }

    devices_.erase(iter);

    if (ChooseDeviceToControl()) {
      ShowSelectedDevice();
      RedrawInteractiveState();
    }
  }

  void OnDeviceGainChanged(uint64_t dev_token, AudioGainInfo info) {
    auto iter = devices_.find(dev_token);
    if (iter == devices_.end()) {
      std::cerr << "\r<WARNING>: Invalid device token (" << dev_token
                << ") during gain changed notification." << std::endl;
      return;
    }

    iter->second.gain_info = std::move(info);
    if (control_token_ == dev_token) {
      RedrawInteractiveState();
    }
  }

  void OnDefaultDeviceChanged(uint64_t old_id, uint64_t new_id) {
    auto old_iter = devices_.find(old_id);
    if (old_iter != devices_.end()) {
      old_iter->second.is_default = false;
    }

    auto new_iter = devices_.find(new_id);
    if (new_iter != devices_.end()) {
      new_iter->second.is_default = true;
    }

    if (ChooseDeviceToControl()) {
      ShowSelectedDevice();
      RedrawInteractiveState();
    }
  }

  bool interactive() const { return non_interactive_actions_.empty(); }

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  fit::closure quit_callback_;
  std::deque<fit::closure> non_interactive_actions_;
  fuchsia::media::AudioDeviceEnumeratorPtr audio_;
  uint64_t control_token_ = ZX_KOID_INVALID;
  uint64_t selected_token_ = ZX_KOID_INVALID;
  std::string selected_uid_;
  bool input_ = false;
  std::map<uint64_t, AudioDeviceInfo> devices_;
  EscapeDecoder esc_decoder_;
  fsl::FDWaiter fd_waiter_;
};

}  // namespace media

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  media::VolApp app(argc, argv, [&loop]() {
    async::PostTask(loop.async(), [&loop]() { loop.Quit(); });
  });
  loop.Run();
  return 0;
}
