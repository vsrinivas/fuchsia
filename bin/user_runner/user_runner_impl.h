// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_USER_RUNNER_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_USER_RUNNER_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/firebase/cpp/fidl.h>
#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/speech/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/component/cpp/service_provider_impl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/user_runner/agent_runner/agent_runner_storage_impl.h"
#include "peridot/bin/user_runner/entity_provider_runner/entity_provider_launcher.h"
#include "peridot/bin/user_runner/entity_provider_runner/entity_provider_runner.h"
#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/scope.h"
#include "peridot/lib/fidl/view_host.h"
#include "peridot/lib/rapidjson/rapidjson.h"

namespace modular {

class AgentRunner;
class ComponentContextImpl;
class DeviceMapImpl;
class FocusHandler;
class LedgerClient;
class LinkImpl;
class MessageQueueManager;
class PuppetMasterImpl;
class SessionStorage;
class StoryCommandExecutor;
class StoryProviderImpl;
class StoryStorage;
class VisibleStoriesHandler;

class UserRunnerImpl : fuchsia::modular::internal::UserRunner,
                       fuchsia::modular::UserShellContext,
                       EntityProviderLauncher {
 public:
  UserRunnerImpl(component::StartupContext* startup_context, bool test);

  ~UserRunnerImpl() override;

  // |AppDriver| calls this.
  void Terminate(std::function<void()> done);

 private:
  // |UserRunner|
  void Initialize(
      fuchsia::modular::auth::AccountPtr account,
      fuchsia::modular::AppConfig user_shell,
      fuchsia::modular::AppConfig story_shell,
      fidl::InterfaceHandle<fuchsia::modular::auth::TokenProviderFactory>
          token_provider_factory,
      fidl::InterfaceHandle<fuchsia::modular::internal::UserContext>
          user_context,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request) override;

  // |UserRunner|
  void SwapUserShell(fuchsia::modular::AppConfig user_shell,
                     SwapUserShellCallback callback) override;

  // Sequence of Initialize() broken up into steps for clarity.
  void InitializeUser(
      fuchsia::modular::auth::AccountPtr account,
      fidl::InterfaceHandle<fuchsia::modular::auth::TokenProviderFactory>
          token_provider_factory,
      fidl::InterfaceHandle<fuchsia::modular::internal::UserContext>
          user_context);
  void InitializeLedger();
  void InitializeLedgerDashboard();
  void InitializeDeviceMap();
  void InitializeClipboard();
  void InitializeMessageQueueManager();
  void InitializeMaxwellAndModular(const fidl::StringPtr& user_shell_url,
                                   fuchsia::modular::AppConfig story_shell);
  void InitializeUserShell(
      fuchsia::modular::AppConfig user_shell,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request);

  void RunUserShell(fuchsia::modular::AppConfig user_shell);
  // This is a termination sequence that may be used with |AtEnd()|, but also
  // may be executed to terminate the currently running user shell.
  void TerminateUserShell(const std::function<void()>& done);

  // |fuchsia::modular::UserShellContext|
  void GetAccount(GetAccountCallback callback) override;
  void GetAgentProvider(
      fidl::InterfaceRequest<fuchsia::modular::AgentProvider> request) override;
  void GetComponentContext(
      fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request)
      override;
  void GetDeviceName(GetDeviceNameCallback callback) override;
  void GetFocusController(
      fidl::InterfaceRequest<fuchsia::modular::FocusController> request)
      override;
  void GetFocusProvider(
      fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) override;
  void GetIntelligenceServices(
      fidl::InterfaceRequest<fuchsia::modular::IntelligenceServices> request)
      override;
  void GetLink(fidl::InterfaceRequest<fuchsia::modular::Link> request) override;
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override;
  void GetSpeechToText(
      fidl::InterfaceRequest<fuchsia::speech::SpeechToText> request) override;
  void GetStoryProvider(
      fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) override;
  void GetSuggestionProvider(
      fidl::InterfaceRequest<fuchsia::modular::SuggestionProvider> request)
      override;
  void GetVisibleStoriesController(
      fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesController>
          request) override;
  void Logout() override;

  // |EntityProviderLauncher|
  void ConnectToEntityProvider(
      const std::string& component_id,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
          entity_provider_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController>
          agent_controller_request) override;

  fuchsia::sys::ServiceProviderPtr GetServiceProvider(
      fuchsia::modular::AppConfig config);
  fuchsia::sys::ServiceProviderPtr GetServiceProvider(const std::string& url);

  fuchsia::ledger::cloud::CloudProviderPtr GetCloudProvider();

  // Called during initialization. Schedules the given action to be executed
  // during termination. This allows to create something like an asynchronous
  // destructor at initialization time. The sequence of actions thus scheduled
  // is executed in reverse in Terminate().
  //
  // The AtEnd() calls for a field should happen next to the calls that
  // initialize the field, for the following reasons:
  //
  // 1. It ensures the termination sequence corresponds to the initialization
  //    sequence.
  //
  // 2. It is easy to audit that there is a termination action for every
  //    initialization that needs one.
  //
  // 3. Conditional initialization also omits the termination (such as for
  //    agents that are not started when runnign a test).
  //
  // See also the Reset() and Teardown() functions in the .cc file.
  void AtEnd(std::function<void(std::function<void()>)> action);

  // Recursively execute the termiation steps scheduled by AtEnd(). The
  // execution steps are stored in at_end_.
  void TerminateRecurse(int i);

  component::StartupContext* const startup_context_;
  const bool test_;

  fidl::BindingSet<fuchsia::modular::internal::UserRunner> bindings_;
  fidl::Binding<fuchsia::modular::UserShellContext> user_shell_context_binding_;

  fuchsia::modular::auth::TokenProviderFactoryPtr token_provider_factory_;
  fuchsia::modular::internal::UserContextPtr user_context_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> cloud_provider_app_;
  fuchsia::ledger::cloud::firebase::FactoryPtr cloud_provider_factory_;
  std::unique_ptr<AppClient<fuchsia::ledger::internal::LedgerController>>
      ledger_app_;
  fuchsia::ledger::internal::LedgerRepositoryFactoryPtr
      ledger_repository_factory_;
  fuchsia::ledger::internal::LedgerRepositoryPtr ledger_repository_;
  std::unique_ptr<LedgerClient> ledger_client_;
  // Provides services to the Ledger
  component::ServiceProviderImpl ledger_service_provider_;

  std::unique_ptr<Scope> user_scope_;

  fuchsia::modular::auth::AccountPtr account_;

  std::unique_ptr<AppClient<fuchsia::modular::UserIntelligenceProviderFactory>>
      maxwell_app_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> context_engine_app_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> module_resolver_app_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> user_shell_app_;
  fuchsia::modular::UserShellPtr user_shell_;
  std::unique_ptr<ViewHost> user_shell_view_host_;

  std::unique_ptr<EntityProviderRunner> entity_provider_runner_;

  std::unique_ptr<SessionStorage> session_storage_;
  AsyncHolder<StoryProviderImpl> story_provider_impl_;
  std::unique_ptr<MessageQueueManager> message_queue_manager_;
  std::unique_ptr<AgentRunnerStorage> agent_runner_storage_;
  AsyncHolder<AgentRunner> agent_runner_;
  std::unique_ptr<DeviceMapImpl> device_map_impl_;
  std::string device_name_;

  std::unique_ptr<StoryCommandExecutor> story_command_executor_;
  std::unique_ptr<PuppetMasterImpl> puppet_master_impl_;

  // Services we provide to |context_engine_app_|.
  component::ServiceProviderImpl context_engine_ns_services_;

  // These component contexts are supplied to:
  // - the user intelligence provider (from |maxwell_app_|) so it can run agents
  //   and create message queues
  // - |context_engine_app_| so it can resolve entity references
  // - |modular resolver_service_| so it can resolve entity references
  std::unique_ptr<fidl::BindingSet<fuchsia::modular::ComponentContext,
                                   std::unique_ptr<ComponentContextImpl>>>
      maxwell_component_context_bindings_;

  // Service provider interfaces for maxwell services. They are created with
  // the component context above as parameters.
  fidl::InterfacePtr<fuchsia::modular::UserIntelligenceProvider>
      user_intelligence_provider_;
  fidl::InterfacePtr<fuchsia::modular::IntelligenceServices>
      intelligence_services_;

  // Services we provide to the module resolver's namespace.
  component::ServiceProviderImpl module_resolver_ns_services_;
  fuchsia::modular::ModuleResolverPtr module_resolver_service_;

  class PresentationProviderImpl;
  std::unique_ptr<PresentationProviderImpl> presentation_provider_impl_;

  std::unique_ptr<FocusHandler> focus_handler_;
  std::unique_ptr<VisibleStoriesHandler> visible_stories_handler_;

  // Component context given to user shell so that it can run agents and
  // create message queues.
  std::unique_ptr<ComponentContextImpl> user_shell_component_context_impl_;

  // Given to the user shell so it can store its own data. These data are
  // shared between all user shells (so it's not private to the user shell
  // *app*).
  std::unique_ptr<StoryStorage> user_shell_storage_;
  fidl::BindingSet<fuchsia::modular::Link, std::unique_ptr<LinkImpl>>
      user_shell_link_bindings_;

  // For the Ledger Debug Dashboard
  std::unique_ptr<Scope> ledger_dashboard_scope_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> ledger_dashboard_app_;

  // Holds the actions scheduled by calls to the AtEnd() method.
  std::vector<std::function<void(std::function<void()>)>> at_end_;

  // Holds the done callback of Terminate() while the at_end_ actions are being
  // executed. We can rely on Terminate() only being called once. (And if not,
  // this could simply be made a vector as usual.)
  std::function<void()> at_end_done_;

  // The service provider used to connect to services advertised by the
  // clipboard agent.
  fuchsia::sys::ServiceProviderPtr services_from_clipboard_agent_;

  // The agent controller used to control the clipboard agent.
  fuchsia::modular::AgentControllerPtr clipboard_agent_controller_;

  class SwapUserShellOperation;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserRunnerImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_USER_RUNNER_IMPL_H_
