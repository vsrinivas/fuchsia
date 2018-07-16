// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>
#include <string>

#include <fs/pseudo-file.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <trace-provider/provider.h>

#include "peridot/bin/device_runner/cobalt/cobalt.h"
#include "peridot/bin/device_runner/user_provider_impl.h"
#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/util/filesystem.h"

namespace modular {

namespace {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    device_shell.url = command_line.GetOptionValueWithDefault(
        "device_shell", "userpicker_device_shell");
    story_shell.url =
        command_line.GetOptionValueWithDefault("story_shell", "mondrian");
    user_runner.url =
        command_line.GetOptionValueWithDefault("user_runner", "user_runner");
    user_shell.url = command_line.GetOptionValueWithDefault(
        "user_shell", "armadillo_user_shell");
    account_provider.url = command_line.GetOptionValueWithDefault(
        "account_provider", "oauth_token_manager");

    disable_statistics = command_line.HasOption("disable_statistics");
    ignore_monitor = command_line.HasOption("ignore_monitor");
    no_minfs = command_line.HasOption("no_minfs");
    test = command_line.HasOption("test");

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("device_shell_args", ""),
        &device_shell.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("story_shell_args", ""),
        &story_shell.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("user_runner_args", ""),
        &user_runner.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("user_shell_args", ""),
        &user_shell.args);

    if (test) {
      device_shell.args.push_back("--test");
      story_shell.args.push_back("--test");
      user_runner.args.push_back("--test");
      user_shell.args.push_back("--test");
      test_name = FindTestName(user_shell.url, user_shell.args);
      disable_statistics = true;
      ignore_monitor = true;
      no_minfs = true;
    }
  }

  static std::string GetUsage() {
    return R"USAGE(device_runner
      --device_shell=DEVICE_SHELL
      --device_shell_args=SHELL_ARGS
      --user_shell=USER_SHELL
      --user_shell_args=SHELL_ARGS
      --story_shell=STORY_SHELL
      --story_shell_args=SHELL_ARGS
      --account_provider=ACCOUNT_PROVIDER
      --disable_statistics
      --ignore_monitor
      --no_minfs
      --test
    DEVICE_NAME: Name which user shell uses to identify this device.
    DEVICE_SHELL: URL of the device shell to run.
                Defaults to "userpicker_device_shell".
                For integration testing use "dev_device_shell".
    USER_RUNNER: URL of the user runner to run.
                Defaults to "user_runner".
    USER_SHELL: URL of the user shell to run.
                Defaults to "armadillo_user_shell".
                For integration testing use "dev_user_shell".
    STORY_SHELL: URL of the story shell to run.
                Defaults to "mondrian".
                For integration testing use "dev_story_shell".
    SHELL_ARGS: Comma separated list of arguments. Backslash escapes comma.
    ACCOUNT_PROVIDER: URL of the account provider to use.
                Defaults to "oauth_token_manager".
                For integration tests use "dev_token_manager".)USAGE";
  }

  fuchsia::modular::AppConfig device_shell;
  fuchsia::modular::AppConfig story_shell;
  fuchsia::modular::AppConfig user_runner;
  fuchsia::modular::AppConfig user_shell;
  fuchsia::modular::AppConfig account_provider;

  std::string test_name;
  bool disable_statistics;
  bool ignore_monitor;
  bool no_minfs;
  bool test;

 private:
  void ParseShellArgs(const std::string& value,
                      fidl::VectorPtr<fidl::StringPtr>* args) {
    bool escape = false;
    std::string arg;
    for (char i : value) {
      if (escape) {
        arg.push_back(i);
        escape = false;
        continue;
      }

      if (i == '\\') {
        escape = true;
        continue;
      }

      if (i == ',') {
        args->push_back(arg);
        arg.clear();
        continue;
      }

      arg.push_back(i);
    }

    if (!arg.empty()) {
      args->push_back(arg);
    }
  }

  // Extract the test name using knowledge of how Modular structures its
  // command lines for testing.
  static std::string FindTestName(
      const fidl::StringPtr& user_shell,
      const fidl::VectorPtr<fidl::StringPtr>& user_shell_args) {
    const std::string kRootModule = "--root_module";
    std::string result = user_shell;

    for (const auto& user_shell_arg : *user_shell_args) {
      const auto& arg = user_shell_arg.get();
      if (arg.substr(0, kRootModule.size()) == kRootModule) {
        result = arg.substr(kRootModule.size());
      }
    }

    const auto index = result.find_last_of('/');
    if (index == std::string::npos) {
      return result;
    }

    return result.substr(index + 1);
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(Settings);
};

}  // namespace

class DeviceRunnerApp : fuchsia::modular::DeviceShellContext,
                        fuchsia::modular::auth::AccountProviderContext {
 public:
  explicit DeviceRunnerApp(
      const Settings& settings,
      std::shared_ptr<component::StartupContext> const context,
      std::function<void()> on_shutdown)
      : settings_(settings),
        user_provider_impl_("UserProviderImpl"),
        context_(std::move(context)),
        on_shutdown_(std::move(on_shutdown)),
        device_shell_context_binding_(this),
        account_provider_context_binding_(this) {
    // 0a. Check if environment handle / services have been initialized.
    if (!context_->has_environment_services()) {
      FXL_LOG(ERROR) << "Failed to receive services from the environment.";
      exit(1);
    }

    // 0b. Connect to the device runner monitor and check this
    // instance is the only one running, unless the command line asks
    // to ignore the monitor check.
    if (settings.ignore_monitor) {
      Start();

    } else {
      context_->ConnectToEnvironmentService(monitor_.NewRequest());

      monitor_.set_error_handler([] {
        FXL_LOG(ERROR) << "No device runner monitor found.";
        exit(1);
      });

      monitor_->GetConnectionCount([this](uint32_t count) {
        if (count != 1) {
          FXL_LOG(ERROR) << "Another device runner is running."
                         << " Please use that one, or shut it down first.";
          exit(1);
        }

        Start();
      });
    }
  }

 private:
  void Start() {
    // 0. Print test banner.
    if (settings_.test) {
      FXL_LOG(INFO)
          << std::endl
          << std::endl
          << "======================== Starting Test [" << settings_.test_name
          << "]" << std::endl
          << "============================================================"
          << std::endl;
    }

    // 1. Start the device shell. This also connects the root view of the device
    // to the device shell. This is done first so that we can show some UI until
    // other things come up.
    device_shell_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            context_->launcher().get(), CloneStruct(settings_.device_shell));
    device_shell_app_->services().ConnectToService(device_shell_.NewRequest());

    fuchsia::ui::views_v1::ViewProviderPtr device_shell_view_provider;
    device_shell_app_->services().ConnectToService(
        device_shell_view_provider.NewRequest());

    // We still need to pass a request for root view to device shell since
    // dev_device_shell (which mimics flutter behavior) blocks until it receives
    // the root view request.
    fidl::InterfaceHandle<fuchsia::ui::views_v1_token::ViewOwner> root_view;
    fuchsia::ui::policy::PresentationPtr presentation;
    device_shell_view_provider->CreateView(root_view.NewRequest(), nullptr);
    if (!settings_.test) {
      context_->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>()
          ->Present(std::move(root_view), presentation.NewRequest());
    }

    // Populate parameters and initialize the device shell.
    fuchsia::modular::DeviceShellParams params;
    params.presentation = std::move(presentation);
    device_shell_->Initialize(device_shell_context_binding_.NewBinding(),
                              std::move(params));

    // 2. Wait for persistent data to come up.
    if (!settings_.no_minfs) {
      WaitForMinfs();
    }

    // 3. Start OAuth Token Manager App.
    fuchsia::modular::AppConfig token_manager_config;
    token_manager_config.url = settings_.account_provider.url;
    token_manager_ =
        std::make_unique<AppClient<fuchsia::modular::auth::AccountProvider>>(
            context_->launcher().get(), std::move(token_manager_config),
            "/data/modular/ACCOUNT_MANAGER");
    token_manager_->SetAppErrorHandler([] {
      FXL_CHECK(false) << "Token manager crashed. Stopping device runner.";
    });
    token_manager_->primary_service()->Initialize(
        account_provider_context_binding_.NewBinding());

    // 4. Setup user provider.
    user_provider_impl_.reset(new UserProviderImpl(
        context_, settings_.user_runner, settings_.user_shell,
        settings_.story_shell, token_manager_->primary_service().get()));

    ReportEvent(ModularEvent::BOOTED_TO_DEVICE_RUNNER);
  }

  // |fuchsia::modular::DeviceShellContext|
  void GetUserProvider(
      fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) override {
    user_provider_impl_->Connect(std::move(request));
  }

  // |fuchsia::modular::DeviceShellContext|
  void Shutdown() override {
    // TODO(mesch): Some of these could be done in parallel too.
    // fuchsia::modular::UserProvider must go first, but the order after user
    // provider is for now rather arbitrary. We terminate device shell last so
    // that in tests testing::Teardown() is invoked at the latest possible time.
    // Right now it just demonstrates that AppTerminate() works as we like it
    // to.
    FXL_DLOG(INFO) << "fuchsia::modular::DeviceShellContext::Shutdown()";

    if (settings_.test) {
      FXL_LOG(INFO)
          << std::endl
          << "============================================================"
          << std::endl
          << "======================== [" << settings_.test_name << "] Done";
    }

    user_provider_impl_.Teardown(kUserProviderTimeout, [this] {
      FXL_DLOG(INFO) << "- fuchsia::modular::UserProvider down";
      token_manager_->Teardown(kBasicTimeout, [this] {
        FXL_DLOG(INFO) << "- AuthProvider down";
        device_shell_app_->Teardown(kBasicTimeout, [this] {
          FXL_DLOG(INFO) << "- fuchsia::modular::DeviceShell down";
          FXL_LOG(INFO) << "Clean Shutdown";
          on_shutdown_();
        });
      });
    });
  }

  // |AccountProviderContext|
  void GetAuthenticationContext(
      fidl::StringPtr account_id,
      fidl::InterfaceRequest<fuchsia::modular::auth::AuthenticationContext>
          request) override {
    device_shell_->GetAuthenticationContext(account_id, std::move(request));
  }

  const Settings& settings_;  // Not owned nor copied.
  AsyncHolder<UserProviderImpl> user_provider_impl_;

  std::shared_ptr<component::StartupContext> const context_;
  fuchsia::modular::DeviceRunnerMonitorPtr monitor_;
  std::function<void()> on_shutdown_;

  fidl::Binding<fuchsia::modular::DeviceShellContext>
      device_shell_context_binding_;
  fidl::Binding<fuchsia::modular::auth::AccountProviderContext>
      account_provider_context_binding_;

  std::unique_ptr<AppClient<fuchsia::modular::auth::AccountProvider>>
      token_manager_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> device_shell_app_;
  fuchsia::modular::DeviceShellPtr device_shell_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerApp);
};

fxl::AutoCall<fit::closure> SetupCobalt(Settings& settings, async_dispatcher_t* dispatcher,
                                        component::StartupContext* context) {
  if (settings.disable_statistics) {
    return fxl::MakeAutoCall<fit::closure>([] {});
  }
  return InitializeCobalt(dispatcher, context);
};

}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    std::cout << modular::Settings::GetUsage() << std::endl;
    return 0;
  }

  modular::Settings settings(command_line);
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  trace::TraceProvider trace_provider(loop.dispatcher());
  auto context = std::shared_ptr<component::StartupContext>(
      component::StartupContext::CreateFromStartupInfo());
  fxl::AutoCall<fit::closure> cobalt_cleanup =
      modular::SetupCobalt(settings, std::move(loop.dispatcher()), context.get());

  modular::DeviceRunnerApp app(settings, context, [&loop, &cobalt_cleanup] {
    cobalt_cleanup.call();
    loop.Quit();
  });
  loop.Run();

  return 0;
}
