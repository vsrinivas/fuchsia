// Copyright 2016 The Fuchsia Authors. All rights reserved.
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
#include <lib/callback/scoped_callback.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/memory/weak_ptr.h>
#include "src/lib/fxl/strings/string_number_conversions.h"

#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"

namespace modular {

namespace {
#ifdef USE_ACCOUNT_MANAGER
constexpr bool kUseAccountManager = true;
#else
constexpr bool kUseAccountManager = false;
#endif
}  // namespace

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    // device_name will be set to the device's hostname if it is empty or null
    device_name = command_line.GetOptionValueWithDefault("device_name", "");

    // default user is guest
    user = command_line.GetOptionValueWithDefault("user", "");

    // If passed, runs as a test harness.
    use_test_runner = command_line.HasOption("use_test_runner");

    test_timeout_ms = testing::kTestTimeoutMilliseconds;

    if (command_line.HasOption("test_timeout_ms")) {
      std::string test_timeout_ms_string;
      command_line.GetOptionValue("test_timeout_ms", &test_timeout_ms_string);
      if (!fxl::StringToNumberWithError<uint64_t>(test_timeout_ms_string,
                                                  &test_timeout_ms)) {
        FXL_LOG(WARNING) << "Unable to parse timeout from '"
                         << test_timeout_ms_string << "'. Setting to default.";
      }
    }
  }

  std::string device_name;
  std::string user;
  uint64_t test_timeout_ms;
  bool use_test_runner{};
};

class DevBaseShellApp : modular::SingleServiceApp<fuchsia::modular::BaseShell> {
 public:
  explicit DevBaseShellApp(component::StartupContext* const startup_context,
                           Settings settings)
      : SingleServiceApp(startup_context),
        settings_(std::move(settings)),
        weak_ptr_factory_(this) {
    this->startup_context()->ConnectToEnvironmentService(
        account_manager_.NewRequest());
    if (settings_.use_test_runner) {
      testing::Init(this->startup_context(), __FILE__);
      testing::Await(testing::kTestShutdown,
                     [this] { base_shell_context_->Shutdown(); });

      // Start a timer to quit in case a test component misbehaves and hangs. If
      // we hit the timeout, this is a test failure.
      async::PostDelayedTask(
          async_get_default_dispatcher(),
          callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                               [this] {
                                 FXL_LOG(WARNING) << "DevBaseShell timed out";
                                 testing::Fail("DevBaseShell timed out");
                                 base_shell_context_->Shutdown();
                               }),
          zx::msec(settings_.test_timeout_ms));
    }
  }

  ~DevBaseShellApp() override = default;

  // |SingleServiceApp|
  void Terminate(fit::function<void()> done) override {
    if (settings_.use_test_runner) {
      testing::Teardown(std::move(done));
    } else {
      done();
    }
  }

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
      if (settings_.user.empty()) {
        // Login as a guest user.
        Login("");
        return;
      }

      // If we are using account manager for authentication, we provision a new
      // auth account with the expectation that basemgr is subscribed as an
      // account listener.
      if (kUseAccountManager) {
        // Ignore settings_user and always create a Fuchsia account if there
        // isn't one already.
        account_manager_->GetAccountIds(
            [this](
                std::vector<fuchsia::auth::account::LocalAccountId> accounts) {
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
      } else {
        user_provider_->PreviousUsers(
            [this](std::vector<fuchsia::modular::auth::Account> accounts) {
              FXL_LOG(INFO)
                  << "Found " << accounts.size() << " users in the user "
                  << "database";

              // Add the user if not already added.
              std::string account_id;
              for (const auto& account : accounts) {
                FXL_LOG(INFO) << "Found user " << account.display_name;
                if (account.display_name.size() >= settings_.user.size() &&
                    account.display_name.substr(0, settings_.user.size()) ==
                        settings_.user) {
                  account_id = account.id;
                  break;
                }
                if (account_id.empty()) {
                  user_provider_->AddUser(
                      fuchsia::modular::auth::IdentityProvider::DEV,
                      [this](fuchsia::modular::auth::AccountPtr account,
                             fidl::StringPtr status) { Login(account->id); });
                } else {
                  Login(account_id);
                }
              }
            });
      }
    }
  }

  const Settings settings_;
  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
      view_owner_request_;
  fuchsia::modular::BaseShellContextPtr base_shell_context_;
  fuchsia::modular::UserProviderPtr user_provider_;

  fuchsia::auth::account::AccountManagerPtr account_manager_;

  fxl::WeakPtrFactory<DevBaseShellApp> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevBaseShellApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  modular::Settings settings(command_line);

  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<modular::DevBaseShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::DevBaseShellApp>(context.get(), settings),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
