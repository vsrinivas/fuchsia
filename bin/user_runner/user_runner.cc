// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the user runner mojo app.

#include <memory>

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/maxwell/services/launcher/launcher.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/strong_binding.h"
#include "apps/modular/services/user/focus.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/modular/src/user_runner/story_provider_impl.h"
#include "apps/modular/src/user_runner/user_ledger_repository_factory.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

namespace {

const char kAppId[] = "modular_user_runner";
const char kLedgerBaseDir[] = "/data/ledger/";
// This is the prefix for the ApplicationEnvironment under which all stories run
// for a user.
const char kStoriesEnvironmentLabelPrefix[] = "stories-";

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

// This environment host is used to run all the stories, and runs under the
// UserRunner's scope. Its ServiceProvider forwards all requests to its parent's
// ServiceProvider.
class UserStoriesScope : public ApplicationEnvironmentHost {
 public:
  UserStoriesScope(ApplicationEnvironmentPtr parent_env,
                   const fidl::Array<uint8_t>& user_id)
      : binding_(this), parent_env_(std::move(parent_env)) {
    // Set up a new ApplicationEnvironment under which we run all stories.
    ApplicationEnvironmentHostPtr env_host;
    binding_.Bind(env_host.NewRequest());
    parent_env_->CreateNestedEnvironment(
        std::move(env_host), env_.NewRequest(), env_controller_.NewRequest(),
        kStoriesEnvironmentLabelPrefix + to_hex_string(user_id));
  }

  ApplicationEnvironmentPtr GetEnvironment() {
    ApplicationEnvironmentPtr env;
    env_->Duplicate(env.NewRequest());
    return env;
  }

 private:
  // |ApplicationEnvironmentHost|:
  void GetApplicationEnvironmentServices(
      fidl::InterfaceRequest<ServiceProvider> environment_services) override {
    parent_env_->GetServices(std::move(environment_services));
  }

  fidl::Binding<ApplicationEnvironmentHost> binding_;

  ApplicationEnvironmentPtr env_;
  ApplicationEnvironmentPtr parent_env_;
  ApplicationEnvironmentControllerPtr env_controller_;
  ServiceProviderImpl env_services_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UserStoriesScope);
};

class UserRunnerImpl : public UserRunner {
 public:
  UserRunnerImpl(std::shared_ptr<ApplicationContext> application_context,
                 fidl::InterfaceRequest<UserRunner> user_runner_request)
      : application_context_(application_context),
        binding_(this, std::move(user_runner_request)) {}

  ~UserRunnerImpl() override = default;

 private:
  void SetupLedgerRepository(const fidl::Array<uint8_t>& user_id) {
    auto launch_info = ApplicationLaunchInfo::New();

    ServiceProviderPtr app_services;
    launch_info->services = app_services.NewRequest();
    launch_info->url = "file:///system/apps/ledger";

    // Note that |LedgerRepositoryFactory| is started in the device runner's
    // environment.
    application_context_->launcher()->CreateApplication(std::move(launch_info),
                                                        nullptr);

    ledger::LedgerRepositoryFactoryPtr ledger_repository_factory;
    ConnectToService(app_services.get(), ledger_repository_factory.NewRequest());
    ledger_repository_factory_ = std::make_unique<UserLedgerRepositoryFactory>(
        kLedgerBaseDir + to_hex_string(user_id),
        std::move(ledger_repository_factory));
  }

  // |UserRunner|:
  void Initialize(
      fidl::Array<uint8_t> user_id,
      const fidl::String& user_shell,
      fidl::Array<fidl::String> user_shell_args,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) override {
    SetupLedgerRepository(user_id);

    ApplicationEnvironmentPtr parent_env;
    application_context_->environment()->Duplicate(GetProxy(&parent_env));
    stories_scope_ = std::make_unique<UserStoriesScope>(
        std::move(parent_env), user_id);

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

    fidl::InterfaceHandle<StoryProvider> story_provider;
    auto story_provider_impl = new StoryProviderImpl(
        stories_scope_->GetEnvironment(), std::move(ledger),
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

    ApplicationLauncherPtr launcher;
    application_context_->environment()->GetApplicationLauncher(
        launcher.NewRequest());
    launcher->CreateApplication(std::move(launch_info), nullptr);

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

  // The application environment hosted by user runner.
  std::unique_ptr<UserStoriesScope> stories_scope_;

  UserShellPtr user_shell_;

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
