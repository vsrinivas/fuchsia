// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the fuchsia::modular::BaseShell service that passes a
// command line configurable user name to its fuchsia::modular::UserProvider,
// and is able to run a story with a single module through its life cycle.

#include <memory>
#include <utility>

#include <fuchsia/auth/account/cpp/fidl.h>
#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include "peridot/lib/fidl/single_service_app.h"

namespace modular {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    // defaults to auto-logging in as an ephemeral user
    persist_user = command_line.HasOption("persist_user");
  }

  bool persist_user;
};

class AutoLoginBaseShellApp
    : modular::SingleServiceApp<fuchsia::modular::BaseShell> {
 public:
  explicit AutoLoginBaseShellApp(
      component::StartupContext* const startup_context, Settings settings)
      : SingleServiceApp(startup_context),
        settings_(std::move(settings)),
        weak_ptr_factory_(this) {
    this->startup_context()->ConnectToEnvironmentService(
        account_manager_.NewRequest());
  }

  ~AutoLoginBaseShellApp() override = default;

  // |SingleServiceApp|
  void Terminate(fit::function<void()> done) override { done(); }

 private:
  // |SingleServiceApp|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<
          fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<
          fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    view_owner_request_ =
        fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>(
            zx::channel(view_token.release()));
    Connect();
  }

  // |fuchsia::modular::BaseShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::BaseShellContext>
                      base_shell_context,
                  fuchsia::modular::BaseShellParams) override {
    base_shell_context_.Bind(std::move(base_shell_context));
    base_shell_context_->GetUserProvider(user_provider_.NewRequest());

    Connect();
  }

  // |fuchsia::modular::BaseShell|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<
          fuchsia::auth::AuthenticationUIContext> /*request*/) override {
    FXL_LOG(INFO)
        << "fuchsia::modular::BaseShell::GetAuthenticationUIContext() is"
           " unimplemented.";
  }

  void Login(const std::string& account_id) {
    fuchsia::modular::UserLoginParams params;
    params.account_id = account_id;
    params.view_owner = std::move(view_owner_request_);
    user_provider_->Login(std::move(params));
  }

  void Connect() {
    if (user_provider_ && view_owner_request_) {
      if (!settings_.persist_user) {
        // Login as an ephemeral guest user.
        Login("");
        return;
      }

      // We provision a new auth account with the expectation that basemgr is
      // subscribed as an account listener.
      account_manager_->GetAccountIds(
          [this](std::vector<fuchsia::auth::account::LocalAccountId> accounts) {
            if (!accounts.empty()) {
              return;
            }

            account_manager_->ProvisionNewAccount(
                [](fuchsia::auth::account::Status,
                   std::unique_ptr<fuchsia::auth::account::LocalAccountId>) {
                  FXL_LOG(INFO) << "Provisioned new account. Translating "
                                   "this account into a "
                                   "fuchsia::modular::auth::Account.";
                });
          });
    }
  }

  const Settings settings_;
  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
      view_owner_request_;
  fuchsia::modular::BaseShellContextPtr base_shell_context_;
  fuchsia::modular::UserProviderPtr user_provider_;

  fuchsia::auth::account::AccountManagerPtr account_manager_;

  fxl::WeakPtrFactory<AutoLoginBaseShellApp> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AutoLoginBaseShellApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  modular::Settings settings(command_line);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<modular::AutoLoginBaseShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::AutoLoginBaseShellApp>(context.get(), settings),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
