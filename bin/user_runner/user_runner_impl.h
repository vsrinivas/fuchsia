// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_USER_RUNNER_USER_RUNNER_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_USER_RUNNER_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "lib/agent/fidl/agent_controller/agent_controller.fidl.h"
#include "lib/auth/fidl/account/account.fidl.h"
#include "lib/config/fidl/config.fidl.h"
#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/context/fidl/context_writer.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/resolver/fidl/resolver.fidl.h"
#include "lib/story/fidl/story_provider.fidl.h"
#include "lib/suggestion/fidl/suggestion_provider.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "lib/user/fidl/user_runner.fidl.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/bin/agent_runner/agent_runner_storage_impl.h"
#include "peridot/bin/entity/entity_repository.h"
#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/scope.h"
#include "peridot/lib/rapidjson/rapidjson.h"

namespace modular {

class AgentRunner;
class ComponentContextImpl;
class DeviceMapImpl;
class FocusHandler;
class LedgerClient;
class LinkImpl;
class LinkStorage;
class MessageQueueManager;
class RemoteInvokerImpl;
class StoryProviderImpl;
class VisibleStoriesHandler;

class UserRunnerImpl : UserRunner, UserShellContext {
 public:
  UserRunnerImpl(std::shared_ptr<app::ApplicationContext> application_context,
                 bool test);

  ~UserRunnerImpl() override;

  void Connect(fidl::InterfaceRequest<UserRunner> request);

 private:
  // |UserRunner|
  void Initialize(
      auth::AccountPtr account,
      AppConfigPtr user_shell,
      AppConfigPtr story_shell,
      fidl::InterfaceHandle<auth::TokenProviderFactory> token_provider_factory,
      fidl::InterfaceHandle<UserContext> user_context,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) override;

  // |UserRunner|
  void Terminate() override;

  // |UserShellContext|
  void GetAccount(const GetAccountCallback& callback) override;
  void GetAgentProvider(fidl::InterfaceRequest<AgentProvider> request) override;
  void GetContextReader(
      fidl::InterfaceRequest<maxwell::ContextReader> request) override;
  void GetContextWriter(
      fidl::InterfaceRequest<maxwell::ContextWriter> request) override;
  void GetDeviceName(const GetDeviceNameCallback& callback) override;
  void GetFocusController(
      fidl::InterfaceRequest<FocusController> request) override;
  void GetFocusProvider(fidl::InterfaceRequest<FocusProvider> request) override;
  void GetIntelligenceServices(
      fidl::InterfaceRequest<maxwell::IntelligenceServices> request) override;
  void GetLink(fidl::InterfaceRequest<Link> request) override;
  void GetProposalPublisher(
      fidl::InterfaceRequest<maxwell::ProposalPublisher> request) override;
  void GetStoryProvider(fidl::InterfaceRequest<StoryProvider> request) override;
  void GetSuggestionProvider(
      fidl::InterfaceRequest<maxwell::SuggestionProvider> request) override;
  void GetVisibleStoriesController(
      fidl::InterfaceRequest<VisibleStoriesController> request) override;
  void Logout() override;
  void LogoutAndResetLedgerState() override;

  app::ServiceProviderPtr GetServiceProvider(AppConfigPtr config);
  app::ServiceProviderPtr GetServiceProvider(const std::string& url);

  void SetupLedger();

  std::unique_ptr<fidl::Binding<UserRunner>> binding_;
  std::shared_ptr<app::ApplicationContext> application_context_;
  const bool test_;
  fidl::Binding<UserShellContext> user_shell_context_binding_;

  auth::TokenProviderFactoryPtr token_provider_factory_;
  UserContextPtr user_context_;
  std::unique_ptr<AppClient<ledger::LedgerController>> ledger_app_client_;
  ledger::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  ledger::LedgerRepositoryPtr ledger_repository_;
  std::unique_ptr<LedgerClient> ledger_client_;

  std::unique_ptr<Scope> user_scope_;

  auth::AccountPtr account_;

  std::unique_ptr<AppClient<maxwell::UserIntelligenceProviderFactory>> maxwell_;
  std::unique_ptr<AppClient<UserShell>> user_shell_;

  std::unique_ptr<EntityRepository> entity_repository_;
  AsyncHolder<StoryProviderImpl> story_provider_impl_;
  std::unique_ptr<MessageQueueManager> message_queue_manager_;
  std::unique_ptr<AgentRunnerStorage> agent_runner_storage_;
  AsyncHolder<AgentRunner> agent_runner_;
  std::unique_ptr<DeviceMapImpl> device_map_impl_;
  std::unique_ptr<RemoteInvokerImpl> remote_invoker_impl_;
  std::string device_name_;

  // This component context is supplied to the user intelligence provider,
  // below, so it can run agents and create message queues.
  std::unique_ptr<ComponentContextImpl> maxwell_component_context_impl_;
  std::unique_ptr<fidl::Binding<ComponentContext>>
      maxwell_component_context_binding_;

  // Service provider interfaces for maxwell services. They are created with the
  // component context above as parameters.
  fidl::InterfacePtr<maxwell::UserIntelligenceProvider>
      user_intelligence_provider_;
  fidl::InterfacePtr<maxwell::IntelligenceServices> intelligence_services_;

  std::unique_ptr<FocusHandler> focus_handler_;
  std::unique_ptr<VisibleStoriesHandler> visible_stories_handler_;

  // Given to the user shell so it can store its own data. These data are shared
  // between all user shells (so it's not private to the user shell *app*).
  //
  // HACK(mesch): The user shell link must be defined *before* the link storage
  // because it invokes a method of link storage (DropWatcher()) in its
  // destructor, which must happen before the link storage is destroyed.
  std::unique_ptr<LinkStorage> link_storage_;
  std::unique_ptr<LinkImpl> user_shell_link_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserRunnerImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_USER_RUNNER_IMPL_H_
