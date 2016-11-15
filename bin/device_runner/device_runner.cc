// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <iostream>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/application/application_launcher.fidl.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
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

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    user_shell = command_line.GetOptionValueWithDefault(
        "user-shell", "file:///system/apps/armadillo_user_shell");
  }

  static std::string GetUsage() {
    return R"USAGE(device_runner --user-shell=USER_SHELL
    USER_SHELL: URL of the user shell to run.
                Defaults to "file:///system/apps/armadillo_user_shell".)USAGE";
  }

  std::string user_shell;
};

class DeviceRunnerImpl : public DeviceRunner {
 public:
  DeviceRunnerImpl(const Settings& settings,
                   ApplicationLauncherPtr launcher,
                   fidl::InterfaceRequest<DeviceRunner> service)
      : settings_(settings),
        launcher_(std::move(launcher)),
        binding_(this, std::move(service)) {}

  ~DeviceRunnerImpl() override = default;

 private:
  void Login(const fidl::String& username,
             fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) override {
    FTL_LOG(INFO) << "DeviceRunnerImpl::Login() " << username;
    auto launch_info = ApplicationLaunchInfo::New();
    launch_info->url = "file:///system/apps/user_runner";
    ServiceProviderPtr services;
    launch_info->services = GetProxy(&services);
    launcher_->CreateApplication(std::move(launch_info),
                                 GetProxy(&user_runner_controller_));

    ConnectToService(services.get(), GetProxy(&user_runner_));
    user_runner_->Launch(to_array(username), settings_.user_shell,
                         std::move(view_owner_request));
  }

  Settings settings_;
  ApplicationLauncherPtr launcher_;
  StrongBinding<DeviceRunner> binding_;

  ApplicationControllerPtr user_runner_controller_;
  // Interface pointer to the |UserRunner| handle exposed by the User Runner.
  // Currently, we maintain a single instance which means that subsequent
  // logins override previous ones.
  UserRunnerPtr user_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerImpl);
};

class DeviceRunnerApp {
 public:
  DeviceRunnerApp(const Settings& settings)
      : context_(ApplicationContext::CreateFromStartupInfo()) {
    FTL_LOG(INFO) << "DeviceRunnerApp::DeviceRunnerApp()";

    auto launch_info = ApplicationLaunchInfo::New();
    launch_info->url = "file:///system/apps/dummy_device_shell";
    ServiceProviderPtr app_services;
    launch_info->services = GetProxy(&app_services);
    context_->launcher()->CreateApplication(
        std::move(launch_info), GetProxy(&device_shell_controller_));

    mozart::ViewProviderPtr view_provider;
    ConnectToService(app_services.get(), GetProxy(&view_provider));

    fidl::InterfaceHandle<mozart::ViewOwner> root_view;
    view_provider->CreateView(GetProxy(&root_view), nullptr);
    context_->ConnectToEnvironmentService<mozart::Presenter>()->Present(
        std::move(root_view));

    ConnectToService(app_services.get(), GetProxy(&device_shell_));

    ApplicationLauncherPtr launcher;
    context_->environment()->GetApplicationLauncher(GetProxy(&launcher));
    fidl::InterfaceHandle<DeviceRunner> device_runner_handle;
    new DeviceRunnerImpl(settings, std::move(launcher),
                         GetProxy(&device_runner_handle));
    device_shell_->SetDeviceRunner(std::move(device_runner_handle));
  }

 private:
  std::unique_ptr<ApplicationContext> context_;
  ApplicationControllerPtr device_shell_controller_;
  DeviceShellPtr device_shell_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerApp);
};

}  // namespace
}  // namespace modular

int main(int argc, const char** argv) {
  FTL_LOG(INFO) << "device runner main";

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
