// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>
#include <string>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/util/filesystem.h"
#include "apps/modular/services/auth/account_provider.fidl.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/modular/services/device/device_runner_monitor.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/src/device_runner/user_provider_impl.h"
#include "apps/mozart/services/presentation/presenter.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {
namespace {

constexpr char kLedgerAppUrl[] = "file:///system/apps/ledger";
constexpr char kLedgerNoMinfsWaitFlag[] = "--no_minfs_wait";

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    device_shell.url = command_line.GetOptionValueWithDefault(
        "device_shell", "file:///system/apps/userpicker_device_shell");
    user_shell.url = command_line.GetOptionValueWithDefault(
        "user_shell", "file:///system/apps/armadillo_user_shell");
    story_shell.url = command_line.GetOptionValueWithDefault(
        "story_shell", "file:///system/apps/mondrian");

    ledger_repository_for_testing =
        command_line.HasOption("ledger_repository_for_testing");
    user_auth_config_file = command_line.HasOption("user_auth_config_file");
    ignore_monitor = command_line.HasOption("ignore_monitor");
    no_minfs = command_line.HasOption("no_minfs");

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("device_shell_args", ""),
        &device_shell.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("user_shell_args", ""),
        &user_shell.args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("story_shell_args", ""),
        &story_shell.args);
  }

  static std::string GetUsage() {
    return R"USAGE(device_runner
      --device_shell=DEVICE_SHELL
      --device_shell_args=SHELL_ARGS
      --user_shell=USER_SHELL
      --user_shell_args=SHELL_ARGS
      --story_shell=STORY_SHELL
      --story_shell_args=SHELL_ARGS
      --ledger_repository_for_testing
      --user_auth_config_file
      --ignore_monitor
      --no_minfs
    DEVICE_NAME: Name which user shell uses to identify this device.
    DEVICE_SHELL: URL of the device shell to run.
                Defaults to "file:///system/apps/userpicker_device_shell".
    USER_SHELL: URL of the user shell to run.
                Defaults to "file:///system/apps/armadillo_user_shell".
                For integration testing use "dummy_user_shell".
    STORY_SHELL: URL of the story shell to run.
                Defaults to "file:///system/apps/dummy_story_shell".
    SHELL_ARGS: Comma separated list of arguments. Backslash escapes comma.)USAGE";
  }

  AppConfig device_shell;
  AppConfig user_shell;
  AppConfig story_shell;

  bool ledger_repository_for_testing;
  bool user_auth_config_file;
  bool ignore_monitor;
  bool no_minfs;

 private:
  void ParseShellArgs(const std::string& value,
                      fidl::Array<fidl::String>* args) {
    bool escape = false;
    std::string arg;
    for (std::string::const_iterator i = value.begin(); i != value.end(); ++i) {
      if (escape) {
        arg.push_back(*i);
        escape = false;
        continue;
      }

      if (*i == '\\') {
        escape = true;
        continue;
      }

      if (*i == ',') {
        args->push_back(arg);
        arg.clear();
        continue;
      }

      arg.push_back(*i);
    }

    if (!arg.empty()) {
      args->push_back(arg);
    }
  }

  FTL_DISALLOW_COPY_AND_ASSIGN(Settings);
};

class DeviceRunnerApp : DeviceShellContext, auth::AccountProviderContext {
 public:
  DeviceRunnerApp(const Settings& settings)
      : settings_(settings),
        app_context_(
            app::ApplicationContext::CreateFromStartupInfoNotChecked()),
        device_shell_context_binding_(this),
        account_provider_context_(this) {
    // 0a. Check if environment handle / services have been initialized.
    if (!app_context_->launcher()) {
      FTL_LOG(ERROR) << "Environment handle not set. Please use @boot.";
      exit(1);
    }
    if (!app_context_->environment_services()) {
      FTL_LOG(ERROR) << "Services handle not set. Please use @boot.";
      exit(1);
    }

    // 0b. Connect to the device runner monitor and check this
    // instance is the only one running, unless the command line asks
    // to ignore the monitor check.
    if (settings.ignore_monitor) {
      Start();

    } else {
      app_context_->ConnectToEnvironmentService(monitor_.NewRequest());

      monitor_.set_connection_error_handler([] {
        FTL_LOG(ERROR) << "No device runner monitor found. "
                       << " Please use @boot.";
        exit(1);
      });

      monitor_->GetConnectionCount([this](uint32_t count) {
        if (count != 1) {
          FTL_LOG(ERROR) << "Another device runner is running."
                         << " Please use that one, or shut it down first.";
          exit(1);
        }

        Start();
      });
    }
  }

 private:
  void Start() {
    // 1. Start the device shell. This also connects the root view of the device
    // to the device shell. This is done first so that we can show some UI until
    // other things come up.
    app::ServiceProviderPtr device_shell_services;
    auto device_shell_launch_info = app::ApplicationLaunchInfo::New();
    device_shell_launch_info->url = settings_.device_shell.url;
    device_shell_launch_info->arguments = settings_.device_shell.args.Clone();
    device_shell_launch_info->services = device_shell_services.NewRequest();

    app_context_->launcher()->CreateApplication(
        std::move(device_shell_launch_info),
        device_shell_controller_.NewRequest());

    mozart::ViewProviderPtr device_shell_view_provider;
    ConnectToService(device_shell_services.get(),
                     device_shell_view_provider.NewRequest());

    ConnectToService(device_shell_services.get(), device_shell_.NewRequest());

    fidl::InterfaceHandle<mozart::ViewOwner> root_view;
    device_shell_view_provider->CreateView(root_view.NewRequest(), nullptr);
    app_context_->ConnectToEnvironmentService<mozart::Presenter>()->Present(
        std::move(root_view));

    device_shell_->Initialize(device_shell_context_binding_.NewBinding());

    // 2. Wait for persistent data to come up.
    if (!settings_.no_minfs) {
      WaitForMinfs();
    }

    // 3. Start OAuth Token Manager App.
    app::ServiceProviderPtr token_manager_services;
    auto token_manager_launch_info = app::ApplicationLaunchInfo::New();
    token_manager_launch_info->url = "file:///system/apps/oauth_token_manager";
    token_manager_launch_info->services = token_manager_services.NewRequest();
    app_context_->launcher()->CreateApplication(
        std::move(token_manager_launch_info),
        token_manager_controller_.NewRequest());

    auth::AccountProviderPtr account_provider;
    ConnectToService(token_manager_services.get(),
                     account_provider.NewRequest());
    account_provider->Initialize(account_provider_context_.NewBinding());

    // 4. Start the ledger.
    app::ServiceProviderPtr ledger_services;
    auto ledger_launch_info = app::ApplicationLaunchInfo::New();
    ledger_launch_info->url = kLedgerAppUrl;
    ledger_launch_info->arguments = fidl::Array<fidl::String>::New(1);
    ledger_launch_info->arguments[0] = kLedgerNoMinfsWaitFlag;
    ledger_launch_info->services = ledger_services.NewRequest();

    app_context_->launcher()->CreateApplication(
        std::move(ledger_launch_info), ledger_controller_.NewRequest());

    ledger::LedgerRepositoryFactoryPtr ledger_repository_factory;
    ConnectToService(ledger_services.get(),
                     ledger_repository_factory.NewRequest());

    // 5. Setup user provider.
    user_provider_impl_ = std::make_unique<UserProviderImpl>(
        app_context_, settings_.user_shell, settings_.story_shell,
        std::move(ledger_repository_factory),
        settings_.ledger_repository_for_testing, std::move(account_provider));
  }

  // |DeviceShellContext|
  void GetUserProvider(fidl::InterfaceRequest<UserProvider> request) override {
    user_provider_impl_->Connect(std::move(request));
  }

  // |DeviceShellContext|
  // TODO(vardhan): Signal the ledger application to tear down.
  void Shutdown() override {
    FTL_LOG(INFO) << "DeviceShellContext::Shutdown()";
    user_provider_impl_->Teardown(
        [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); });
  }

  // |AccountProviderContext|
  void GetAuthenticationContext(
      const fidl::String& account_id,
      fidl::InterfaceRequest<AuthenticationContext> request) override {
    device_shell_->GetAuthenticationContext(account_id, std::move(request));
  }

  const Settings& settings_;  // Not owned nor copied.
  std::unique_ptr<UserProviderImpl> user_provider_impl_;

  std::shared_ptr<app::ApplicationContext> app_context_;
  DeviceRunnerMonitorPtr monitor_;

  fidl::Binding<DeviceShellContext> device_shell_context_binding_;
  fidl::Binding<auth::AccountProviderContext> account_provider_context_;

  app::ApplicationControllerPtr token_manager_controller_;

  app::ApplicationControllerPtr device_shell_controller_;
  DeviceShellPtr device_shell_;

  app::ApplicationControllerPtr ledger_controller_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerApp);
};

}  // namespace
}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    std::cout << modular::Settings::GetUsage() << std::endl;
    return 0;
  }

  modular::Settings settings(command_line);

  mtl::MessageLoop loop;
  modular::DeviceRunnerApp app(settings);
  loop.Run();
  return 0;
}
