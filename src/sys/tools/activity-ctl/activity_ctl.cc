// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/activity/control/cpp/fidl.h>
#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/cmdline/args_parser.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include <memory>
#include <random>
#include <variant>

#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace activity_ctl {

const char kHelp[] = R"(activity_ctl <command> [ <command_args> ]

  activity_ctl is a command line utility for interacting with the Activity
  Service. This utility can provide input to the activity service or listen
  to the system's state activity state.

Commands

  discrete_activity   - Send a discrete activity
  force_state <state> - Force the activity service into a state
  ongoing_activity    - Initiate an ongoing activity, ending when the utility
                        exits
  watch_state         - Listen for changes to the system's activity state
)";

const char kHelpHelp[] = R"(--help (-h)
    Prints this help and exits)";

constexpr const char kCommandForceState[] = "force_state";
constexpr const char kCommandWatchState[] = "watch_state";
constexpr const char kCommandDiscreteActivity[] = "discrete_activity";
constexpr const char kCommandOngoingActivity[] = "ongoing_activity";

enum Command { Unknown, ForceState, WatchState, SendDiscreteActivity, SendOngoingActivity };

Command ParseCommand(std::string_view cmd) {
  if (cmd == kCommandForceState)
    return Command::ForceState;
  if (cmd == kCommandWatchState)
    return Command::WatchState;
  if (cmd == kCommandDiscreteActivity)
    return Command::SendDiscreteActivity;
  if (cmd == kCommandOngoingActivity)
    return Command::SendOngoingActivity;
  return Command::Unknown;
}

std::variant<std::vector<std::string>, cmdline::Status> ParseCommandLine(int argc,
                                                                         const char* argv[]) {
  cmdline::GeneralArgsParser parser;

  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });

  std::vector<std::string> params;
  cmdline::Status status = parser.ParseGeneral(argc, argv, &params);
  if (status.has_error())
    return status;

  if (requested_help || params.empty())
    return cmdline::Status::Error(kHelp);

  return params;
}

fuchsia::ui::activity::State ParseState(std::string_view state) {
  if (state == "IDLE")
    return fuchsia::ui::activity::State::IDLE;
  if (state == "ACTIVE")
    return fuchsia::ui::activity::State::ACTIVE;
  return fuchsia::ui::activity::State::UNKNOWN;
}

std::string StateToString(fuchsia::ui::activity::State state) {
  if (state == fuchsia::ui::activity::State::IDLE)
    return "IDLE";
  if (state == fuchsia::ui::activity::State::ACTIVE)
    return "ACTIVE";
  return "UNKNOWN";
}

class LoggingListener : public fuchsia::ui::activity::Listener {
 public:
  fidl::InterfaceHandle<fuchsia::ui::activity::Listener> GetHandle(async_dispatcher_t* dispatcher) {
    return binding_.NewBinding(dispatcher);
  }

  void OnStateChanged(fuchsia::ui::activity::State state, zx_time_t transition_time,
                      OnStateChangedCallback callback) override {
    printf("[%lu] %s -> %s\n", transition_time, StateToString(state_).c_str(),
           StateToString(state).c_str());
    state_ = state;
    callback();
  }

 private:
  fuchsia::ui::activity::State state_;
  fidl::Binding<fuchsia::ui::activity::Listener> binding_{this};
};

class ActivityCtl {
 public:
  ActivityCtl(std::unique_ptr<sys::ComponentContext> startup_context,
              async_dispatcher_t* dispatcher, fit::closure quit_callback)
      : context_(std::move(startup_context)),
        quit_callback_(std::move(quit_callback)),
        dispatcher_(dispatcher),
        random_(static_cast<uint32_t>(zx::clock::get_monotonic().get())) {}

  zx_status_t RunCommand(Command cmd, std::vector<std::string> args) {
    switch (cmd) {
      case Command::ForceState: {
        if (args.empty())
          return ZX_ERR_INVALID_ARGS;
        auto state = ParseState(args[0]);
        if (state == fuchsia::ui::activity::State::UNKNOWN)
          return ZX_ERR_INVALID_ARGS;
        ForceState(state);
        break;
      }
      case Command::WatchState: {
        WatchState();
        break;
      }
      case Command::SendDiscreteActivity: {
        SendDiscreteActivity();
        break;
      }
      case Command::SendOngoingActivity: {
        SendOngoingActivity();
        break;
      }
      case Command::Unknown:
      default: {
        return ZX_ERR_INVALID_ARGS;
      }
    }
    return ZX_OK;
  }

  void ForceState(fuchsia::ui::activity::State state) {
    control_conn_ = context_->svc()->Connect<fuchsia::ui::activity::control::Control>();
    (*control_conn_)->SetState(state);
    async::PostTask(dispatcher_, std::move(quit_callback_));
  }

  void WatchState() {
    provider_conn_ = context_->svc()->Connect<fuchsia::ui::activity::Provider>();
    (*provider_conn_)->WatchState(listener_.GetHandle(dispatcher_));
  }

  void SendDiscreteActivity() {}

  void SendOngoingActivity() {}

 private:
  std::optional<fidl::InterfacePtr<fuchsia::ui::activity::Provider>> provider_conn_;
  std::optional<fidl::InterfacePtr<fuchsia::ui::activity::control::Control>> control_conn_;
  LoggingListener listener_;
  std::unique_ptr<sys::ComponentContext> context_;
  fit::closure quit_callback_;
  async_dispatcher_t* dispatcher_;
  std::default_random_engine random_;
};

}  // namespace activity_ctl

int main(int argc, const char** argv) {
  auto status_or_params = activity_ctl::ParseCommandLine(argc, argv);
  if (std::holds_alternative<cmdline::Status>(status_or_params)) {
    fprintf(stderr, "%s\n", std::get<cmdline::Status>(status_or_params).error_message().c_str());
    return 1;
  }
  auto params = std::get<std::vector<std::string>>(status_or_params);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_set_default_dispatcher(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> startup_context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  activity_ctl::ActivityCtl ctl{std::move(startup_context), loop.dispatcher(),
                                [&loop]() { loop.Quit(); }};
  auto cmd = activity_ctl::ParseCommand(params[0]);
  if (cmd == activity_ctl::Command::Unknown) {
    fprintf(stderr, "Unknown command: %s\n%s\n", params[0].c_str(), activity_ctl::kHelp);
    return 1;
  }
  params.erase(params.begin());
  if (auto status = ctl.RunCommand(cmd, params) != ZX_OK) {
    fprintf(stderr, "Error: %s\n%s\n", zx_status_get_string(status), activity_ctl::kHelp);
    return 1;
  }

  loop.Run();
  async_set_default_dispatcher(nullptr);
  return 0;
}
