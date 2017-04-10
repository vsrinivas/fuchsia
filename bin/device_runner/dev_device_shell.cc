// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the DeviceShell service that passes a dummy user
// name to its UserProvider.

#include <memory>

#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/device/device_context.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    device_name =
        command_line.GetOptionValueWithDefault("device_name", "magenta");
    user = command_line.GetOptionValueWithDefault("user", "user1");
  }

  std::string device_name;
  std::string user;
};

class DevDeviceShellApp
    : modular::SingleServiceViewApp<modular::DeviceShellFactory>,
      modular::DeviceShell,
      modular::UserWatcher {
 public:
  DevDeviceShellApp(const Settings& settings)
      : settings_(settings),
        device_shell_binding_(this),
        user_watcher_binding_(this) {}
  ~DevDeviceShellApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> services) override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |DeviceShellFactory|
  void Create(fidl::InterfaceHandle<modular::DeviceContext> device_context,
              fidl::InterfaceHandle<modular::UserProvider> user_provider,
              fidl::InterfaceRequest<modular::DeviceShell> device_shell_request)
      override {
    user_provider_.Bind(std::move(user_provider));
    device_context_.Bind(std::move(device_context));

    FTL_DCHECK(!device_shell_binding_.is_bound());
    device_shell_binding_.Bind(std::move(device_shell_request));

    Connect();
  }

  // |DeviceShell|
  void Terminate(const TerminateCallback& done) override {
    FTL_LOG(INFO) << "DeviceShell::Terminate()";
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    done();
  }

  // |UserWatcher|
  void OnLogout() override {
    FTL_LOG(INFO) << "UserWatcher::OnLogout()";
    device_context_->Shutdown();
  }

  void Connect() {
    if (user_provider_ && view_owner_request_) {
      user_provider_->AddUser(settings_.user, nullptr, settings_.device_name,
                              "ledger.fuchsia.com");
      user_provider_->Login(settings_.user, nullptr, nullptr,
                            std::move(view_owner_request_),
                            user_controller_.NewRequest());
      user_controller_->Watch(user_watcher_binding_.NewBinding());
    }
  }

  const Settings settings_;
  fidl::Binding<modular::DeviceShell> device_shell_binding_;
  fidl::Binding<modular::UserWatcher> user_watcher_binding_;
  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  modular::DeviceContextPtr device_context_;
  modular::UserControllerPtr user_controller_;
  modular::UserProviderPtr user_provider_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DevDeviceShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  mtl::MessageLoop loop;
  DevDeviceShellApp app(settings);
  loop.Run();
  return 0;
}
