// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the user runner mojo app.

#include <memory>

#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/mojo/strong_binding.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/modular/src/user_runner/story_provider_impl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

namespace {

std::string LedgerStatusToString(ledger::Status status) {
  switch (status) {
    case ledger::Status::OK:
      return "OK";
    case ledger::Status::AUTHENTICATION_ERROR:
      return "AUTHENTICATION_ERROR";
    case ledger::Status::PAGE_NOT_FOUND:
      return "PAGE_NOT_FOUND";
    case ledger::Status::KEY_NOT_FOUND:
      return "KEY_NOT_FOUND";
    case ledger::Status::REFERENCE_NOT_FOUND:
      return "REFERENCE_NOT_FOUND";
    case ledger::Status::IO_ERROR:
      return "IO_ERROR";
    case ledger::Status::TRANSACTION_ALREADY_IN_PROGRESS:
      return "TRANSACTION_ALREADY_IN_PROGRESS";
    case ledger::Status::NO_TRANSACTION_IN_PROGRESS:
      return "NO_TRANSACTION_IN_PROGRESS";
    case ledger::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case ledger::Status::UNKNOWN_ERROR:
      return "UNKNOWN_ERROR";
    default:
      return "(unknown error)";
  }
};

}  // namespace

class UserRunnerImpl : public UserRunner {
 public:
  UserRunnerImpl(std::shared_ptr<ApplicationContext> application_context,
                 fidl::InterfaceRequest<UserRunner> user_runner_request)
      : application_context_(application_context),
        binding_(this, std::move(user_runner_request)) {}

  ~UserRunnerImpl() override = default;

 private:
  // |UserRunner|:
  void Launch(ledger::IdentityPtr identity,
              fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
              const LaunchCallback& callback) override {
    FTL_LOG(INFO) << "UserRunnerImpl::Launch()";

    auto launch_info = ApplicationLaunchInfo::New();

    ServiceProviderPtr app_services;
    launch_info->services = GetProxy(&app_services);
    launch_info->url = "file:///system/apps/ledger";

    application_context_->launcher()->CreateApplication(std::move(launch_info),
                                                        nullptr);

    ConnectToService(app_services.get(), GetProxy(&ledger_factory_));

    ledger::LedgerPtr ledger;
    auto request = GetProxy(&ledger);
    ledger_factory_->GetLedger(
        std::move(identity), std::move(request), ftl::MakeCopyable([
          this, callback, view_owner_request = std::move(view_owner_request),
          ledger = std::move(ledger)
        ](ledger::Status status) mutable {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "UserRunnerImpl::Launch():"
                           << " LedgerFactory.GetLedger() failed:"
                           << " " << LedgerStatusToString(status) << ".";
            callback(false);
            return;
          }

          callback(true);
          StartUserShell(std::move(ledger), std::move(view_owner_request));
        }));
  }

  // Run the User shell and provide it the |StoryProvider| interface.
  void StartUserShell(
      fidl::InterfaceHandle<ledger::Ledger> ledger,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
    // First use ViewProvider service to plumb |view_owner_request| and get the
    // associated service provider.
    auto launch_info = ApplicationLaunchInfo::New();

    ServiceProviderPtr app_services;
    launch_info->services = GetProxy(&app_services);
    launch_info->url = "file:///system/apps/dummy_user_shell";

    application_context_->launcher()->CreateApplication(std::move(launch_info),
                                                        nullptr);

    mozart::ViewProviderPtr view_provider;
    ConnectToService(app_services.get(), GetProxy(&view_provider));

    ServiceProviderPtr view_services;
    view_provider->CreateView(std::move(view_owner_request),
                              GetProxy(&view_services));

    view_providers_.AddInterfacePtr(std::move(view_provider));

    // Use this service provider to get |UserShell| interface.
    ConnectToService(view_services.get(), GetProxy(&user_shell_));

    fidl::InterfaceHandle<StoryProvider> story_provider;
    new StoryProviderImpl(application_context_, std::move(ledger),
                          GetProxy(&story_provider));

    user_shell_->SetStoryProvider(std::move(story_provider));
  }

  std::shared_ptr<ApplicationContext> application_context_;
  StrongBinding<UserRunner> binding_;

  fidl::InterfacePtrSet<mozart::ViewProvider> view_providers_;
  UserShellPtr user_shell_;

  ledger::LedgerFactoryPtr ledger_factory_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UserRunnerImpl);
};

class UserRunnerApp {
 public:
  UserRunnerApp()
      : application_context_(ApplicationContext::CreateFromStartupInfo()) {
    application_context_->outgoing_services()->AddService<UserRunner>(
        [this](fidl::InterfaceRequest<UserRunner> request) {
          new UserRunnerImpl(application_context_, std::move(request));
        });
  }

 private:
  std::shared_ptr<ApplicationContext> application_context_;
  FTL_DISALLOW_COPY_AND_ASSIGN(UserRunnerApp);
};

}  // namespace modular

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::UserRunnerApp app;
  loop.Run();
  return 0;
}
