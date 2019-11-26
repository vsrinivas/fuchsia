// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the fuchsia::modular::BaseShell service that passes a
// command line configurable user name to its fuchsia::modular::UserProvider,
// and is able to run a story with a single module through its life cycle.

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/identity/account/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>
#include <utility>

#include "src/lib/callback/scoped_callback.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/modular/lib/app_driver/cpp/app_driver.h"
#include "src/modular/lib/fidl/single_service_app.h"
#include "src/modular/lib/integration_testing/cpp/reporting.h"
#include "src/modular/lib/integration_testing/cpp/testing.h"

namespace modular {

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    // device_name will be set to the device's hostname if it is empty or null
    device_name = command_line.GetOptionValueWithDefault("device_name", "");

    // default user is guest
    user = command_line.GetOptionValueWithDefault("user", "");

    // If passed, runs as a test harness.
    use_test_runner = command_line.HasOption("use_test_runner");

    test_timeout_ms = modular_testing::kTestTimeoutMilliseconds;

    if (command_line.HasOption("test_timeout_ms")) {
      std::string test_timeout_ms_string;
      command_line.GetOptionValue("test_timeout_ms", &test_timeout_ms_string);
      if (!fxl::StringToNumberWithError<uint64_t>(test_timeout_ms_string, &test_timeout_ms)) {
        FXL_LOG(WARNING) << "Unable to parse timeout from '" << test_timeout_ms_string
                         << "'. Setting to default.";
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
  explicit DevBaseShellApp(sys::ComponentContext* const component_context, Settings settings)
      : SingleServiceApp(component_context),
        settings_(std::move(settings)),
        weak_ptr_factory_(this) {
    this->component_context()->svc()->Connect(account_manager_.NewRequest());
    if (settings_.use_test_runner) {
      modular_testing::Init(this->component_context(), __FILE__);
      // Start a timer to quit in case a test component misbehaves and hangs. If
      // we hit the timeout, this is a test failure.
      async::PostDelayedTask(async_get_default_dispatcher(),
                             callback::MakeScoped(weak_ptr_factory_.GetWeakPtr(),
                                                  [this] {
                                                    FXL_LOG(WARNING) << "DevBaseShell timed out";
                                                    modular_testing::Fail("DevBaseShell timed out");
                                                    Terminate([] {});
                                                  }),
                             zx::msec(settings_.test_timeout_ms));
    }
  }

  ~DevBaseShellApp() override = default;

  // |SingleServiceApp|
  void Terminate(fit::function<void()> done) override {
    if (settings_.use_test_runner) {
      modular_testing::Teardown(std::move(done));
    } else {
      done();
    }
  }

 private:
  // |SingleServiceApp|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> /*outgoing_services*/) override {
    view_token_.value = std::move(view_token);

    Connect();
  }

  // |fuchsia::modular::BaseShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::BaseShellContext> base_shell_context,
                  fuchsia::modular::BaseShellParams) override {
    base_shell_context_.Bind(std::move(base_shell_context));
    base_shell_context_->GetUserProvider(user_provider_.NewRequest());

    Connect();
  }

  // |fuchsia::modular::BaseShell|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> /*request*/) override {
    FXL_LOG(INFO) << "fuchsia::modular::BaseShell::GetAuthenticationUIContext() is"
                     " unimplemented.";
  }

  void Login(const std::string& account_id) {
    fuchsia::modular::UserLoginParams2 params;
    params.account_id = account_id;
    user_provider_->Login2(std::move(params));
  }

  void Connect() {
    if (user_provider_ && view_token_.value) {
      if (settings_.user.empty()) {
        // Login as a guest user.
        Login("");
        return;
      }

      // We provision a new auth account with the expectation that basemgr is
      // subscribed as an account listener.
      account_manager_->GetAccountIds([this](std::vector<uint64_t> accounts) {
        if (!accounts.empty()) {
          return;
        }

        account_manager_->ProvisionNewAccount(
            fuchsia::identity::account::Lifetime::PERSISTENT, nullptr, [](auto) {
              FXL_LOG(INFO) << "Provisioned new account. Translating "
                               "this account into a "
                               "fuchsia::modular::auth::Account.";
            });
      });
    }
  }

  const Settings settings_;
  fuchsia::ui::views::ViewToken view_token_;
  fuchsia::modular::BaseShellContextPtr base_shell_context_;
  fuchsia::modular::UserProviderPtr user_provider_;

  fuchsia::identity::account::AccountManagerPtr account_manager_;

  fxl::WeakPtrFactory<DevBaseShellApp> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DevBaseShellApp);
};  // namespace modular

}  // namespace modular

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  modular::Settings settings(command_line);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::Create();
  modular::AppDriver<modular::DevBaseShellApp> driver(
      context->outgoing(), std::make_unique<modular::DevBaseShellApp>(context.get(), settings),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
