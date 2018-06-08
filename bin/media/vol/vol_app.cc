// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
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

static constexpr float kGainUnchanged = 1.0f;
static constexpr float kUnityGain = 0.0f;
static constexpr int kLevelMax = 25;
static constexpr char kClearEol[] = "\x1b[K";
static constexpr char kHideCursor[] = "\x1b[?25l";
static constexpr char kShowCursor[] = "\x1b[?25h";

}  // namespace

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

    if (command_line.HasOption("show")) {
      interactive_ = false;
    }

    if (command_line.HasOption("mute")) {
      mute_ = true;
      interactive_ = false;
    }

    if (command_line.HasOption("unmute")) {
      if (mute_) {
        Usage();
        return;
      }

      unmute_ = true;
      interactive_ = false;
    }

    std::string string_value;
    if (command_line.GetOptionValue("gain", &string_value)) {
      if (!Parse(string_value, &gain_db_)) {
        Usage();
        return;
      }

      interactive_ = false;
    }

    audio_ =
        startup_context_->ConnectToEnvironmentService<fuchsia::media::Audio>();
    audio_.set_error_handler([this]() {
      std::cout << "System error: audio service failure";
      quit_callback_();
    });

    audio_.events().SystemGainMuteChanged = [this](float gain_db, bool muted) {
      HandleGainMuteChanged(gain_db, muted);
    };

    if (mute_) {
      audio_->SetSystemMute(true);
    }

    if (unmute_) {
      audio_->SetSystemMute(false);
    }

    if (gain_db_ != kGainUnchanged) {
      audio_->SetSystemGain(gain_db_);
    }

    if (interactive_) {
      std::cout << "\ninteractive mode:\n";
      std::cout << "    +            increase system gain\n";
      std::cout << "    -            decrease system gain\n";
      std::cout << "    m            toggle mute\n";
      std::cout << "    enter        quit\n\n" << kHideCursor;

      setbuf(stdin, nullptr);
    }
  }

 private:
  void Usage() {
    std::cout << "\n";
    std::cout << "vol <args>\n";
    std::cout << "    --show       show system audio status\n";
    std::cout << "    --gain=<db>  set system audio gain\n";
    std::cout << "    --mute       mute system audio\n";
    std::cout << "    --unmute     unmute system audio\n\n";
    std::cout << "Given no arguments, vol waits for the following keystrokes\n";
    std::cout << "    +            increase system gain\n";
    std::cout << "    -            decrease system gain\n";
    std::cout << "    m            toggle mute\n";
    std::cout << "    enter        quit\n";
    std::cout << "\n";

    quit_callback_();
  }

  bool Parse(const std::string& string_value, float* float_out) {
    FXL_DCHECK(float_out);

    std::istringstream istream(string_value);
    return (istream >> *float_out) && istream.eof();
  }

  void HandleGainMuteChanged(float gain_db, bool muted) {
    system_audio_gain_db_ = gain_db;
    system_audio_muted_ = muted;

    if (interactive_) {
      std::cout << "\r";
      FormatGainMute(std::cout);
      std::cout << kClearEol << std::flush;
      if (first_status_) {
        first_status_ = false;
        WaitForKeystroke();
      }
    } else {
      FormatGainMute(std::cout);
      std::cout << std::endl;
      quit_callback_();
      return;
    }
  }

  void FormatGainMute(std::ostream& os) {
    int level = PerceivedLevel::GainToLevel(system_audio_gain_db_, kLevelMax);

    os << std::string(level, '=') << "|" << std::string(kLevelMax - level, '-');

    if (system_audio_gain_db_ == fuchsia::media::kMutedGain) {
      os << " -infinity db";
    } else if (system_audio_gain_db_ == kUnityGain) {
      os << " 0.0 db";
    } else {
      os << " " << std::fixed << std::setprecision(1) << system_audio_gain_db_
         << "db";
    }

    if (system_audio_muted_) {
      os << " muted";
    }
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
    int c = getc(stdin);

    switch (c) {
      case '+':
      case 'A':  // Because <esc>[A is the up key
      case 'C':  // Because <esc>[C is the right key
        audio_->SetSystemGain(PerceivedLevel::LevelToGain(
            PerceivedLevel::GainToLevel(system_audio_gain_db_, kLevelMax) + 1,
            kLevelMax));
        break;
      case '-':
      case 'B':  // Because <esc>[B is the down key
      case 'D':  // Because <esc>[D is the left key
        audio_->SetSystemGain(PerceivedLevel::LevelToGain(
            PerceivedLevel::GainToLevel(system_audio_gain_db_, kLevelMax) - 1,
            kLevelMax));
        break;
      case 'm':
      case 'M':
        audio_->SetSystemMute(!system_audio_muted_);
        break;
      case '\n':
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

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  fit::closure quit_callback_;
  fuchsia::media::AudioPtr audio_;
  bool interactive_ = true;
  bool mute_ = false;
  bool unmute_ = false;
  float gain_db_ = kGainUnchanged;
  fsl::FDWaiter fd_waiter_;
  float system_audio_gain_db_;
  bool system_audio_muted_;
  bool first_status_ = true;
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
