// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the user runner app.

#include <memory>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/maxwell/services/launcher/launcher.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/scope.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/modular/services/user/story_provider.fidl.h"
#include "apps/modular/src/user_runner/story_provider_impl.h"
#include "apps/modular/src/user_runner/user_ledger_repository_factory.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

namespace {

const char kAppId[] = "modular_user_runner";
const char kLedgerBaseDir[] = "/data/ledger/";
// This is the prefix for the ApplicationEnvironment under which all
// stories run for a user.
const char kStoriesScopeLabelPrefix[] = "stories-";

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
  UserRunnerImpl(
      std::shared_ptr<ApplicationContext> application_context,
      fidl::Array<uint8_t> user_id,
      const fidl::String& user_shell,
      fidl::Array<fidl::String> user_shell_args,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<UserRunner> user_runner_request)
      : application_context_(application_context),
        binding_(this, std::move(user_runner_request)) {
    const std::string label = kStoriesScopeLabelPrefix + to_hex_string(user_id);

    SetupLedgerRepository(user_id);

    ApplicationEnvironmentPtr parent_env;
    application_context_->environment()->Duplicate(parent_env.NewRequest());
    stories_scope_ = std::make_unique<Scope>(
        std::move(parent_env), label);

    RunUserShell(user_shell, user_shell_args, std::move(view_owner_request));

    ledger::LedgerRepositoryPtr ledger_repository
        = ledger_repository_factory_->Clone();
    ledger::LedgerPtr ledger;
    ledger_repository->GetLedger(
        to_array(kAppId), ledger.NewRequest(), [](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "UserRunnerImpl::Initialize: "
                          " LedgerRepository.GetLedger() failed: "
                       << LedgerStatusToString(status);
      }
    });

    ApplicationEnvironmentPtr env;
    stories_scope_->environment()->Duplicate(env.NewRequest());

    fidl::InterfaceHandle<StoryProvider> story_provider;
    auto story_provider_impl = new StoryProviderImpl(
        std::move(env), std::move(ledger),
        story_provider.NewRequest(), ledger_repository_factory_.get());

    auto maxwell_services =
        GetServiceProvider("file:///system/apps/maxwell_launcher", nullptr);

    auto maxwell_launcher =
        ConnectToService<maxwell::Launcher>(maxwell_services.get());
    fidl::InterfaceHandle<StoryProvider> story_provider_aux;
    story_provider_impl->AddAuxiliaryBinding(story_provider_aux.NewRequest());

    // The FocusController is implemented by the UserShell.
    fidl::InterfaceHandle<FocusController> focus_controller;
    auto focus_controller_request = focus_controller.NewRequest();
    maxwell_launcher->Initialize(std::move(story_provider_aux),
                                 std::move(focus_controller));

    auto suggestion_provider =
        ConnectToService<maxwell::SuggestionProvider>(maxwell_services.get());

    user_shell_->Initialize(std::move(story_provider),
                            std::move(suggestion_provider),
                            std::move(focus_controller_request));
  }

  ~UserRunnerImpl() override = default;

 private:
  void SetupLedgerRepository(const fidl::Array<uint8_t>& user_id) {
    auto app_services = GetServiceProvider("file:///system/apps/ledger", nullptr);

    ledger::LedgerRepositoryFactoryPtr ledger_repository_factory;
    ConnectToService(app_services.get(), ledger_repository_factory.NewRequest());
    ledger_repository_factory_ = std::make_unique<UserLedgerRepositoryFactory>(
        kLedgerBaseDir + to_hex_string(user_id),
        std::move(ledger_repository_factory));
  }

  ServiceProviderPtr GetServiceProvider(
      const fidl::String& url,
      const fidl::Array<fidl::String>* const args) {
    auto launch_info = ApplicationLaunchInfo::New();

    ServiceProviderPtr services;
    launch_info->services = services.NewRequest();
    launch_info->url = url;
    if (args != nullptr) {
      launch_info->arguments = args->Clone();
    }

    ApplicationControllerPtr ctrl;
    application_context_->launcher()->CreateApplication(
        std::move(launch_info), ctrl.NewRequest());
    application_controllers_.emplace_back(std::move(ctrl));

    return services;
  }

  // This method starts UserShell in a new process, connects to its
  // |ViewProvider| interface, passes a |ViewOwner| request, gets
  // |ServiceProvider| and finally connects to UserShell.
  void RunUserShell(
      const fidl::String& user_shell,
      const fidl::Array<fidl::String>& user_shell_args,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
    auto app_services = GetServiceProvider(user_shell, &user_shell_args);

    mozart::ViewProviderPtr view_provider;
    ConnectToService(app_services.get(), view_provider.NewRequest());
    view_provider->CreateView(std::move(view_owner_request), nullptr);

    // Use this service provider to get |UserShell| interface.
    ConnectToService(app_services.get(), user_shell_.NewRequest());
  }

  std::shared_ptr<ApplicationContext> application_context_;
  StrongBinding<UserRunner> binding_;
  std::unique_ptr<UserLedgerRepositoryFactory> ledger_repository_factory_;
  std::unique_ptr<Scope> stories_scope_;
  UserShellPtr user_shell_;

  // Keep connections to applications started here around so they are
  // killed when this instance is deleted.
  std::vector<ApplicationControllerPtr> application_controllers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UserRunnerImpl);
};

class UserRunnerApp : public UserRunnerFactory {
 public:
  UserRunnerApp()
      : application_context_(ApplicationContext::CreateFromStartupInfo()) {
    application_context_->outgoing_services()->AddService<UserRunnerFactory>(
        [this](fidl::InterfaceRequest<UserRunnerFactory> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

 private:
  // |UserRunnerFactory|
  void Create(
      fidl::Array<uint8_t> user_id,
      const fidl::String& user_shell,
      fidl::Array<fidl::String> user_shell_args,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<UserRunner> user_runner_request) override {
    new UserRunnerImpl(application_context_,
                       std::move(user_id), user_shell, std::move(user_shell_args),
                       std::move(view_owner_request), std::move(user_runner_request));
  }

  std::shared_ptr<ApplicationContext> application_context_;
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
