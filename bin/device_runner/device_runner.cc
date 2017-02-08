// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/device/device_context.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/src/device_runner/user_controller_impl.h"
#include "apps/mozart/services/presentation/presenter.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {
namespace {

constexpr char kLedgerAppUrl[] = "file:///system/apps/ledger";
constexpr char kLedgerDataBaseDir[] = "/data/ledger/";

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    device_shell = command_line.GetOptionValueWithDefault(
        "device_shell", "file:///system/apps/dummy_device_shell");
    user_runner = command_line.GetOptionValueWithDefault(
        "user_runner", "file:///system/apps/user_runner");
    user_shell = command_line.GetOptionValueWithDefault(
        "user_shell", "file:///system/apps/armadillo_user_shell");

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("device_shell_args", ""),
        &device_shell_args);

    ParseShellArgs(
        command_line.GetOptionValueWithDefault("user_shell_args", ""),
        &user_shell_args);
  }

  static std::string GetUsage() {
    return R"USAGE(device_runner
      --device_shell=DEVICE_SHELL
      --device_shell_args=SHELL_ARGS
      --user_runner=USER_RUNNER
      --user_shell=USER_SHELL
      --user_shell_args=SHELL_ARGS
    DEVICE_SHELL: URL of the device shell to run.
                Defaults to "file:///system/apps/dummy_device_shell".
    USER_RUNNER: URL of the user runner implementation to run.
                Defaults to "file:///system/apps/user_runner"
    USER_SHELL: URL of the user shell to run.
                Defaults to "file:///system/apps/armadillo_user_shell".
                For integration testing use "dummy_user_shell".
    SHELL_ARGS: Comma separated list of arguments. Backslash escapes comma.)USAGE";
  }

  std::string device_shell;
  std::vector<std::string> device_shell_args;
  std::string user_runner;
  std::string user_shell;
  std::vector<std::string> user_shell_args;

 private:
  void ParseShellArgs(const std::string& value,
                      std::vector<std::string>* args) {
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

// TODO(vardhan): A running user's state is starting to grow, so break it
// outside of DeviceRunnerApp.
class DeviceRunnerApp : public UserProvider, public DeviceContext {
 public:
  DeviceRunnerApp(const Settings& settings)
      : settings_(settings),
        app_context_(ApplicationContext::CreateFromStartupInfo()),
        device_context_binding_(this),
        user_provider_binding_(this) {
    // 1. Start the ledger.
    ServiceProviderPtr ledger_services;
    auto ledger_launch_info = ApplicationLaunchInfo::New();
    ledger_launch_info->url = kLedgerAppUrl;
    ledger_launch_info->arguments = nullptr;
    ledger_launch_info->services = ledger_services.NewRequest();

    app_context_->launcher()->CreateApplication(
        std::move(ledger_launch_info), ledger_controller_.NewRequest());

    ConnectToService(ledger_services.get(),
                     ledger_repository_factory_.NewRequest());

    // 2. Start the device shell.
    ServiceProviderPtr services;
    auto launch_info = ApplicationLaunchInfo::New();
    launch_info->url = settings_.device_shell;
    launch_info->arguments = to_array(settings_.device_shell_args);
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

    device_shell_factory->Create(device_context_binding_.NewBinding(),
                                 user_provider_binding_.NewBinding(),
                                 device_shell_.NewRequest());
  }

 private:
  // |DeviceContext|
  // TODO(vardhan): Signal the ledger application to tear down.
  void Shutdown() override {
    FTL_LOG(INFO) << "Shutting down DeviceRunner..";
    auto cont = [] { mtl::MessageLoop::GetCurrent()->PostQuitTask(); };
    if ((bool)user_controller_impl_) {
      // This will tear down the user runner altogether, and call us back to
      // delete |user_controller_impl_| when it is ready to be killed.
      user_controller_impl_->Logout(cont);
      // At this point, |user_controller_impl_| is destroyed.
    } else {
      cont();
    }
  }

  // |UserProvider|
  void Login(const fidl::String& username,
             fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
             fidl::InterfaceRequest<UserController> user_controller) override {
    // Get the LedgerRepository for the user.
    fidl::Array<uint8_t> user_id = to_array(username);
    fidl::InterfaceHandle<ledger::LedgerRepository> ledger_repository;
    ledger_repository_factory_->GetRepository(
        kLedgerDataBaseDir + to_hex_string(user_id),
        ledger_repository.NewRequest(), [](ledger::Status status) {
          FTL_DCHECK(status == ledger::Status::OK)
              << "GetRepository failed: " << status;
        });

    user_controller_impl_.reset(new UserControllerImpl(
        app_context_, settings_.user_runner, settings_.user_shell,
        settings_.user_shell_args, std::move(user_id),
        std::move(ledger_repository), std::move(view_owner_request),
        std::move(user_controller),
        [this]() { user_controller_impl_.reset(); }));
  }

  const Settings settings_;

  std::shared_ptr<ApplicationContext> app_context_;
  fidl::Binding<DeviceContext> device_context_binding_;
  fidl::Binding<UserProvider> user_provider_binding_;

  ApplicationControllerPtr device_shell_controller_;
  DeviceShellPtr device_shell_;

  ledger::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  ApplicationControllerPtr ledger_controller_;

  std::unique_ptr<UserControllerImpl> user_controller_impl_;

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
