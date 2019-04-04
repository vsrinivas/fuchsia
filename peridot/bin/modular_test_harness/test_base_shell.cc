// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/logging.h>

#include "peridot/lib/fidl/single_service_app.h"

namespace modular {
namespace {

// Implementation of a minimal base shell that will auto-login for testing
// purposes.
class TestBaseShellApp
    : modular::SingleServiceApp<fuchsia::modular::BaseShell> {
 public:
  explicit TestBaseShellApp(component::StartupContext* const startup_context)
      : SingleServiceApp(startup_context) {}

  ~TestBaseShellApp() override = default;

  // |SingleServiceApp|
  void Terminate(fit::function<void()> done) override { done(); }

  // move-only.
  TestBaseShellApp(const TestBaseShellApp&) = delete;
  void operator=(const TestBaseShellApp&) = delete;

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

    LoginIfReady();
  }

  // |fuchsia::modular::BaseShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::BaseShellContext>
                      base_shell_context,
                  fuchsia::modular::BaseShellParams) override {
    base_shell_context_.Bind(std::move(base_shell_context));
    base_shell_context_->GetUserProvider(user_provider_.NewRequest());

    LoginIfReady();
  }

  // |fuchsia::modular::BaseShell|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<
          fuchsia::auth::AuthenticationUIContext> /*request*/) override {
    FXL_LOG(INFO)
        << "fuchsia::modular::BaseShell::GetAuthenticationUIContext() is"
           " unimplemented.";
  }

  // We get here from both Initialize() or CreateView().
  void LoginIfReady() {
    if (!user_provider_ || !view_owner_request_) {
      return;
    }

    fuchsia::modular::UserLoginParams params;
    params.account_id = "";  // incognito mode
    params.view_owner = std::move(view_owner_request_);
    user_provider_->Login(std::move(params));
  }

  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
      view_owner_request_;
  fuchsia::modular::BaseShellContextPtr base_shell_context_;
  fuchsia::modular::UserProviderPtr user_provider_;
};

}  // namespace
}  // namespace modular

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = component::StartupContext::CreateFromStartupInfo();

  modular::AppDriver<modular::TestBaseShellApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::TestBaseShellApp>(context.get()),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
