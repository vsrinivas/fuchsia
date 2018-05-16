// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <iomanip>
#include <iostream>

#include <audio_policy/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/functional/closure.h"
#include "lib/media/audio/perceived_level.h"

using audio_policy::AudioPolicyStatus;

namespace media {
namespace {

static constexpr float kGainUnchanged = 1.0f;
static constexpr float kUnityGain = 0.0f;
static constexpr int kLevelMax = 25;
static constexpr char kClearEol[] = "\x1b[K";
static constexpr char kHideCursor[] = "\x1b[?25l";
static constexpr char kShowCursor[] = "\x1b[?25h";

}  // namespace

std::ostream& operator<<(std::ostream& os, const AudioPolicyStatus& value) {
  int level =
      PerceivedLevel::GainToLevel(value.system_audio_gain_db, kLevelMax);

  os << std::string(level, '=') << "|" << std::string(kLevelMax - level, '-');

  if (value.system_audio_gain_db == kMutedGain) {
    os << " -infinity db";
  } else if (value.system_audio_gain_db == kUnityGain) {
    os << " 0.0 db";
  } else {
    os << " " << std::fixed << std::setprecision(1)
       << value.system_audio_gain_db << "db";
  }

  if (value.system_audio_muted) {
    os << " muted";
  }

  return os;
}

class VolApp {
 public:
  VolApp(int argc, const char** argv, fxl::Closure quit_callback)
      : application_context_(
            component::ApplicationContext::CreateFromStartupInfo()),
        quit_callback_(quit_callback) {
    FXL_DCHECK(quit_callback);

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

    audio_policy_service_ =
        application_context_
            ->ConnectToEnvironmentService<audio_policy::AudioPolicy>();
    audio_policy_service_.set_error_handler([this]() {
      std::cout << "System error: audio policy service failure";
      quit_callback_();
    });

    if (mute_) {
      audio_policy_service_->SetSystemAudioMute(true);
    }

    if (unmute_) {
      audio_policy_service_->SetSystemAudioMute(false);
    }

    if (gain_db_ != kGainUnchanged) {
      audio_policy_service_->SetSystemAudioGain(gain_db_);
    }

    HandleStatus();
    audio_policy_service_->GetStatus(
        kInitialStatus, [this](uint64_t version, AudioPolicyStatus status) {});

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

  void HandleStatus(uint64_t version = kInitialStatus,
                    audio_policy::AudioPolicyStatusPtr status = nullptr) {
    if (status) {
      system_audio_gain_db_ = status->system_audio_gain_db;
      system_audio_muted_ = status->system_audio_muted;

      if (interactive_) {
        std::cout << "\r" << *status << kClearEol << std::flush;
        if (first_status_) {
          first_status_ = false;
          WaitForKeystroke();
        }
      } else {
        std::cout << *status << std::endl;
        quit_callback_();
        return;
      }
    }

    audio_policy_service_->GetStatus(
        version, [this](uint64_t version, AudioPolicyStatus status) {
          HandleStatus(version, fidl::MakeOptional(std::move(status)));
        });
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
        audio_policy_service_->SetSystemAudioGain(PerceivedLevel::LevelToGain(
            PerceivedLevel::GainToLevel(system_audio_gain_db_, kLevelMax) + 1,
            kLevelMax));
        break;
      case '-':
      case 'B':  // Because <esc>[B is the down key
      case 'D':  // Because <esc>[D is the left key
        audio_policy_service_->SetSystemAudioGain(PerceivedLevel::LevelToGain(
            PerceivedLevel::GainToLevel(system_audio_gain_db_, kLevelMax) - 1,
            kLevelMax));
        break;
      case 'm':
      case 'M':
        audio_policy_service_->SetSystemAudioMute(!system_audio_muted_);
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

  std::unique_ptr<component::ApplicationContext> application_context_;
  fxl::Closure quit_callback_;
  audio_policy::AudioPolicyPtr audio_policy_service_;
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
