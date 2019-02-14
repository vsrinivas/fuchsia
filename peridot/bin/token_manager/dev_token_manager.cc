// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/connect.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/macros.h>
#include <trace-provider/provider.h>

namespace fuchsia {
namespace modular {
namespace auth {

class AccountProviderImpl : AccountProvider, ::fuchsia::modular::Lifecycle {
 public:
  AccountProviderImpl(async::Loop* loop);

 private:
  // |AccountProvider|
  void Terminate() override;
  void AddAccount(fuchsia::modular::auth::IdentityProvider identity_provider,
                  AddAccountCallback callback) override;
  void RemoveAccount(fuchsia::modular::auth::Account account, bool revoke_all,
                     RemoveAccountCallback callback) override;

  std::string GenerateAccountId();

  async::Loop* const loop_;
  std::shared_ptr<component::StartupContext> startup_context_;
  fidl::Binding<AccountProvider> binding_;
  fidl::Binding<::fuchsia::modular::Lifecycle> lifecycle_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AccountProviderImpl);
};

AccountProviderImpl::AccountProviderImpl(async::Loop* loop)
    : loop_(loop),
      startup_context_(component::StartupContext::CreateFromStartupInfo()),
      binding_(this),
      lifecycle_binding_(this) {
  FXL_DCHECK(loop);
  startup_context_->outgoing().AddPublicService<AccountProvider>(
      [this](fidl::InterfaceRequest<AccountProvider> request) {
        binding_.Bind(std::move(request));
      });
  startup_context_->outgoing().AddPublicService<::fuchsia::modular::Lifecycle>(
      [this](fidl::InterfaceRequest<::fuchsia::modular::Lifecycle> request) {
        lifecycle_binding_.Bind(std::move(request));
      });
}

void AccountProviderImpl::Terminate() { loop_->Quit(); }

std::string AccountProviderImpl::GenerateAccountId() {
  uint32_t random_number = 0;
  zx_cprng_draw(&random_number, sizeof random_number);
  return std::to_string(random_number);
}

void AccountProviderImpl::AddAccount(
    fuchsia::modular::auth::IdentityProvider identity_provider,
    AddAccountCallback callback) {
  auto account = fuchsia::modular::auth::Account::New();
  account->id = GenerateAccountId();
  account->identity_provider = identity_provider;
  account->display_name = "";
  account->url = "";
  account->image_url = "";

  switch (identity_provider) {
    case fuchsia::modular::auth::IdentityProvider::DEV:
      callback(std::move(account), nullptr);
      return;
    default:
      callback(nullptr, "Unrecognized Identity Provider");
  }
}

void AccountProviderImpl::RemoveAccount(fuchsia::modular::auth::Account account,
                                        bool revoke_all,
                                        RemoveAccountCallback callback) {}

}  // namespace auth
}  // namespace modular
}  // namespace fuchsia

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  fuchsia::modular::auth::AccountProviderImpl app(&loop);
  loop.Run();
  return 0;
}
