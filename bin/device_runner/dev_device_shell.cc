// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the fuchsia::modular::DeviceShell service that passes a
// command line configurable user name to its fuchsia::modular::UserProvider,
// and is able to run a story with a single module through its life cycle.

#include <memory>
#include <utility>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace modular {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    // device_name will be set to the device's hostname if it is empty or null
    device_name = command_line.GetOptionValueWithDefault("device_name", "");

    // default user is incognito
    user = command_line.GetOptionValueWithDefault("user", "");

    // If passed, runs as a test harness.
    test = command_line.HasOption("test");
  }

  std::string device_name;
  std::string user;
  bool test{};
};

class DevDeviceShellApp
    : modular::SingleServiceApp<fuchsia::modular::DeviceShell>,
      fuchsia::modular::UserWatcher {
 public:
  explicit DevDeviceShellApp(
      component::StartupContext* const startup_context, Settings settings)
      : SingleServiceApp(startup_context),
        settings_(std::move(settings)),
        user_watcher_binding_(this),
        weak_ptr_factory_(this) {
    if (settings_.test) {
      testing::Init(this->startup_context(), __FILE__);
      testing::Await(testing::kTestShutdown,
                     [this] { device_shell_context_->Shutdown(); });

      // Start a timer to quit in case a test component misbehaves and hangs.
      async::PostDelayedTask(
          async_get_default_dispatcher(),
          callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                               [this] {
                                 FXL_LOG(WARNING) << "DevDeviceShell timed out";
                                 device_shell_context_->Shutdown();
                               }),
          zx::msec(testing::kTestTimeoutMilliseconds));
    }
  }

  ~DevDeviceShellApp() override = default;

  // |SingleServiceApp|
  void Terminate(std::function<void()> done) override {
    if (settings_.test) {
      testing::Teardown(done);
    } else {
      done();
    }
  }

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*services*/)
      override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |fuchsia::modular::DeviceShell|
  void Initialize(
      fidl::InterfaceHandle<fuchsia::modular::DeviceShellContext>
          device_shell_context,
      fuchsia::modular::DeviceShellParams device_shell_params) override {
    device_shell_context_.Bind(std::move(device_shell_context));
    device_shell_context_->GetUserProvider(user_provider_.NewRequest());

    Connect();
  }

  // |fuchsia::modular::DeviceShell|
  void GetAuthenticationContext(
      fidl::StringPtr /*username*/,
      fidl::InterfaceRequest<
          fuchsia::modular::auth::AuthenticationContext> /*request*/) override {
    FXL_LOG(INFO) << "fuchsia::modular::DeviceShell::GetAuthenticationContext()"
                     " is unimplemented.";
  }

  // |fuchsia::modular::UserWatcher|
  void OnLogout() override {
    FXL_LOG(INFO) << "fuchsia::modular::UserWatcher::OnLogout()";
    device_shell_context_->Shutdown();
  }

  void Login(const std::string& account_id) {
    fuchsia::modular::UserLoginParams params;
    params.account_id = account_id;
    params.view_owner = std::move(view_owner_request_);
    params.user_controller = user_controller_.NewRequest();
    user_provider_->Login(std::move(params));
    user_controller_->Watch(user_watcher_binding_.NewBinding());
  }

  void Connect() {
    if (user_provider_ && view_owner_request_) {
      if (settings_.user.empty()) {
        // Incognito mode.
        Login("");
        return;
      }

      user_provider_->PreviousUsers(
          [this](fidl::VectorPtr<fuchsia::modular::auth::Account> accounts) {
            FXL_LOG(INFO) << "Found " << accounts->size()
                          << " users in the user "
                          << "database";

            // Not running in incognito mode. Add the user if not already
            // added.
            std::string account_id;
            for (const auto& account : *accounts) {
              FXL_LOG(INFO) << "Found user " << account.display_name;
              if (account.display_name->size() >= settings_.user.size() &&
                  account.display_name->substr(settings_.user.size()) ==
                      settings_.user) {
                account_id = account.id;
                break;
              }
            }
            if (account_id.empty()) {
              user_provider_->AddUser(
                  fuchsia::modular::auth::IdentityProvider::DEV,
                  [this](fuchsia::modular::auth::AccountPtr account,
                         fidl::StringPtr status) { Login(account->id); });
            } else {
              Login(account_id);
            }
          });
    }
  }

  const Settings settings_;
  fidl::Binding<fuchsia::modular::UserWatcher> user_watcher_binding_;
  fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
      view_owner_request_;
  fuchsia::modular::DeviceShellContextPtr device_shell_context_;
  fuchsia::modular::UserControllerPtr user_controller_;
  fuchsia::modular::UserProviderPtr user_provider_;
  fxl::WeakPtrFactory<DevDeviceShellApp> weak_ptr_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(DevDeviceShellApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  modular::Settings settings(command_line);

  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<modular::DevDeviceShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::DevDeviceShellApp>(context.get(), settings),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
