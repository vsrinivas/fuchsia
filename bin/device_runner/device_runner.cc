// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <iostream>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/scope.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/mozart/services/presentation/presenter.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {
namespace {

constexpr char kUserScopeLabelPrefix[] = "user-";
constexpr char kUserRunnerUrl[] = "file:///system/apps/user_runner";

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    device_shell = command_line.GetOptionValueWithDefault(
        "device-shell", "file:///system/apps/dummy_device_shell");
    user_shell = command_line.GetOptionValueWithDefault(
        "user-shell", "file:///system/apps/armadillo_user_shell");

    ParseShellArgs(command_line.GetOptionValueWithDefault(
        "device-shell-args", ""), &device_shell_args);

    ParseShellArgs(command_line.GetOptionValueWithDefault(
        "user-shell-args", ""), &user_shell_args);
  }

  static std::string GetUsage() {
    return R"USAGE(device_runner
      --device-shell=DEVICE_SHELL
      --device-shell-args=SHELL_ARGS
      --user-shell=USER_SHELL
      --user-shell-args=SHELL_ARGS
    DEVICE_SHELL: URL of the device shell to run.
                Defaults to "file:///system/apps/dummy_device_shell".
    USER_SHELL: URL of the user shell to run.
                Defaults to "file:///system/apps/armadillo_user_shell".
                For integration testing use "dummy_user_shell".
    SHELL_ARGS: Comma separated list of arguments. Backslash escapes comma.)USAGE";
  }

  std::string device_shell;
  std::vector<std::string> device_shell_args;
  std::string user_shell;
  std::vector<std::string> user_shell_args;

 private:
  void ParseShellArgs(const std::string& value, std::vector<std::string>* args) {
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

    for (auto& s : *args) {
      FTL_LOG(INFO) << "ARG " << s;
    }
  }
};

class DeviceRunnerApp : public UserProvider {
 public:
  DeviceRunnerApp(const Settings& settings)
      : settings_(settings),
        app_context_(ApplicationContext::CreateFromStartupInfo()),
        binding_(this) {
    auto launch_info = ApplicationLaunchInfo::New();
    launch_info->url = settings_.device_shell;
    launch_info->arguments = to_array(settings_.device_shell_args);

    ServiceProviderPtr services;
    launch_info->services = services.NewRequest();
    app_context_->launcher()->CreateApplication(
        std::move(launch_info), device_shell_controller_.NewRequest());

    mozart::ViewProviderPtr view_provider;
    ConnectToService(services.get(), view_provider.NewRequest());

    DeviceShellFactoryPtr device_shell_factory;
    ConnectToService(services.get(), device_shell_factory.NewRequest());

    fidl::InterfaceHandle<mozart::ViewOwner> root_view;
    view_provider->CreateView(root_view.NewRequest(), nullptr);
    app_context_->ConnectToEnvironmentService<mozart::Presenter>()->Present(
        std::move(root_view));

    fidl::InterfaceHandle<UserProvider> user_provider_handle;
    binding_.Bind(user_provider_handle.NewRequest());
    device_shell_factory->Create(std::move(user_provider_handle),
                                 device_shell_.NewRequest());
  }

 private:
  // |UserProvider|
  void Login(
      const fidl::String& username,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) override {
    fidl::Array<uint8_t> user_id = to_array(username);
    const std::string label = kUserScopeLabelPrefix + to_hex_string(user_id);

    // 1. Create a child environment for the UserRunner.
    ApplicationEnvironmentPtr env;
    app_context_->environment()->Duplicate(env.NewRequest());
    user_runner_scope_ = std::make_unique<Scope>(std::move(env), label);

    ApplicationLauncherPtr launcher;
    user_runner_scope_->environment()->GetApplicationLauncher(
        launcher.NewRequest());

    // 2. Launch UserRunner in the new environment.
    auto launch_info = ApplicationLaunchInfo::New();
    launch_info->url = kUserRunnerUrl;
    ServiceProviderPtr services;
    launch_info->services = services.NewRequest();
    launcher->CreateApplication(
        std::move(launch_info),
        user_runner_controller_.NewRequest());

    // 3. Initialize the UserRunner service.
    UserRunnerFactoryPtr user_runner_factory;
    ConnectToService(services.get(), user_runner_factory.NewRequest());
    user_runner_factory->Create(std::move(user_id), settings_.user_shell,
                                to_array(settings_.user_shell_args),
                                std::move(view_owner_request),
                                user_runner_.NewRequest());
  }

  const Settings settings_;

  std::shared_ptr<ApplicationContext> app_context_;
  fidl::Binding<UserProvider> binding_;

  ApplicationControllerPtr device_shell_controller_;
  DeviceShellPtr device_shell_;

  std::unique_ptr<Scope> user_runner_scope_;
  ApplicationControllerPtr user_runner_controller_;
  UserRunnerPtr user_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerApp);
};

}  // namespace
}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("--help")) {
    std::cout << modular::Settings::GetUsage() << std::endl;
    return 0;
  }

  modular::Settings settings(command_line);

  mtl::MessageLoop loop;
  modular::DeviceRunnerApp app(settings);
  loop.Run();
  return 0;
}
