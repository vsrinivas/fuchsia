// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/src/user_runner/user_runner_impl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

// Implementation of the user runner app.
class UserRunnerApp : UserRunnerFactory {
 public:
  UserRunnerApp()
      : application_context_(app::ApplicationContext::CreateFromStartupInfo()) {
    application_context_->outgoing_services()->AddService<UserRunnerFactory>(
        [this](fidl::InterfaceRequest<UserRunnerFactory> request) {
          bindings_.AddBinding(this, std::move(request));
        });
    tracing::InitializeTracer(application_context_.get(), {"user_runner"});
  }

 private:
  // |UserRunnerFactory|
  void Create(
      auth::AccountPtr account,
      AppConfigPtr user_shell,
      AppConfigPtr story_shell,
      fidl::InterfaceHandle<auth::TokenProviderFactory> token_provider_factory,
      fidl::InterfaceHandle<ledger::LedgerRepository> ledger_repository,
      fidl::InterfaceHandle<UserContext> user_context,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<UserRunner> user_runner_request) override {
    // Deleted in UserRunnerImpl::Terminate().
    new UserRunnerImpl(application_context_->environment(), std::move(account),
                       std::move(user_shell), std::move(story_shell),
                       std::move(ledger_repository),
                       std::move(token_provider_factory),
                       std::move(user_context), std::move(view_owner_request),
                       std::move(user_runner_request));
  }

  std::shared_ptr<app::ApplicationContext> application_context_;
  fidl::BindingSet<UserRunnerFactory> bindings_;
  FTL_DISALLOW_COPY_AND_ASSIGN(UserRunnerApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::UserRunnerApp app;
  loop.Run();
  return 0;
}
