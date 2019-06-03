// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_SESSIONMGR_IMPL_H_
#define PERIDOT_BIN_SESSIONMGR_SESSIONMGR_IMPL_H_

#include <fuchsia/app/discover/cpp/fidl.h>
#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cloud/firestore/cpp/fidl.h>
#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/speech/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/component/cpp/service_provider_impl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>

#include <memory>
#include <string>
#include <vector>

#include "peridot/bin/sessionmgr/agent_runner/agent_runner_storage_impl.h"
#include "peridot/bin/sessionmgr/agent_runner/agent_service_index.h"
#include "peridot/bin/sessionmgr/argv_injecting_launcher.h"
#include "peridot/bin/sessionmgr/entity_provider_runner/entity_provider_launcher.h"
#include "peridot/bin/sessionmgr/entity_provider_runner/entity_provider_runner.h"
#include "peridot/bin/sessionmgr/user_intelligence_provider_impl.h"
#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/environment.h"
#include "peridot/lib/fidl/view_host.h"
#include "peridot/lib/module_manifest/module_facet_reader.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace modular {

class AgentRunner;
class ComponentContextImpl;
class DeviceMapImpl;
class FocusHandler;
class LedgerClient;
class LinkImpl;
class MessageQueueManager;
class PuppetMasterImpl;
class SessionCtl;
class SessionStorage;
class StoryCommandExecutor;
class StoryProviderImpl;
class StoryStorage;
class VisibleStoriesHandler;

class SessionmgrImpl : fuchsia::modular::internal::Sessionmgr,
                       fuchsia::modular::SessionShellContext,
                       EntityProviderLauncher {
 public:
  SessionmgrImpl(component::StartupContext* startup_context,
                 fuchsia::modular::session::SessionmgrConfig config);
  ~SessionmgrImpl() override;

  // |AppDriver| calls this.
  void Terminate(fit::function<void()> callback);

 private:
  // |Sessionmgr|
  void Initialize(
      std::string session_id, fuchsia::modular::auth::AccountPtr account,
      fuchsia::modular::AppConfig session_shell_config,
      fuchsia::modular::AppConfig story_shell_config,
      bool use_session_shell_for_story_shell_factory,
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager,
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager,
      fidl::InterfaceHandle<fuchsia::modular::internal::SessionContext>
          session_context,
      fuchsia::ui::views::ViewToken view_token) override;

  // |Sessionmgr|
  void SwapSessionShell(fuchsia::modular::AppConfig session_shell_config,
                        SwapSessionShellCallback callback) override;

  // Sequence of Initialize() broken up into steps for clarity.
  // TODO(MF-279): Remove |account| and |agent_token_manager| once sessions
  // start receiving persona handles.
  void InitializeSessionEnvironment(std::string session_id);
  void InitializeUser(
      fuchsia::modular::auth::AccountPtr account,
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> agent_token_manager);
  void InitializeLedger(
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager);
  void InitializeDeviceMap();
  void InitializeClipboard();
  void InitializeMessageQueueManager();
  void InitializeMaxwellAndModular(
      const fidl::StringPtr& session_shell_url,
      fuchsia::modular::AppConfig story_shell_config,
      bool use_session_shell_for_story_shell_factory);
  void InitializeDiscovermgr();
  void InitializeSessionShell(fuchsia::modular::AppConfig session_shell_config,
                              fuchsia::ui::views::ViewToken view_token);

  void RunSessionShell(fuchsia::modular::AppConfig session_shell_config);
  // This is a termination sequence that may be used with |AtEnd()|, but also
  // may be executed to terminate the currently running session shell.
  void TerminateSessionShell(fit::function<void()> done);

  // Returns the file descriptor that backs the ledger repository directory for
  // the user.
  zx::channel GetLedgerRepositoryDirectory();

  // |fuchsia::modular::SessionShellContext|
  void GetAccount(
      fit::function<void(::std::unique_ptr<::fuchsia::modular::auth::Account>)>
          callback) override;
  void GetAgentProvider(
      fidl::InterfaceRequest<fuchsia::modular::AgentProvider> request) override;
  void GetComponentContext(
      fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request)
      override;
  void GetDeviceName(fit::function<void(::std::string)> callback) override;
  void GetFocusController(
      fidl::InterfaceRequest<fuchsia::modular::FocusController> request)
      override;
  void GetFocusProvider(
      fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) override;
  void GetLink(fidl::InterfaceRequest<fuchsia::modular::Link> request) override;
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override;
  void GetSpeechToText(
      fidl::InterfaceRequest<fuchsia::speech::SpeechToText> request) override;
  void GetStoryProvider(
      fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) override;
  void GetVisibleStoriesController(
      fidl::InterfaceRequest<fuchsia::modular::VisibleStoriesController>
          request) override;
  void Logout() override;
  void Shutdown();

  // |EntityProviderLauncher|
  void ConnectToEntityProvider(
      const std::string& agent_url,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
          entity_provider_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController>
          agent_controller_request) override;

  // |EntityProviderLauncher|
  void ConnectToStoryEntityProvider(
      const std::string& story_id,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
          entity_provider_request) override;

  fuchsia::sys::ServiceProviderPtr GetServiceProvider(
      fuchsia::modular::AppConfig config);
  fuchsia::sys::ServiceProviderPtr GetServiceProvider(const std::string& url);

  fuchsia::ledger::cloud::CloudProviderPtr LaunchCloudProvider(
      const std::string& user_profile_id,
      fidl::InterfaceHandle<fuchsia::auth::TokenManager> ledger_token_manager);

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
  void AtEnd(fit::function<void(fit::function<void()>)> action);

  // Recursively execute the termiation steps scheduled by AtEnd(). The
  // execution steps are stored in at_end_.
  void TerminateRecurse(int i);

  void ConnectSessionShellToStoryProvider();

  // The device-local unique identifier for this session. The uniqueness
  // is enforced by basemgr which vends sessions.
  std::string session_id_;

  component::StartupContext* const startup_context_;
  fuchsia::modular::session::SessionmgrConfig config_;
  std::unique_ptr<scoped_tmpfs::ScopedTmpFS> memfs_for_ledger_;

  fidl::BindingSet<fuchsia::modular::internal::Sessionmgr> bindings_;
  component::ServiceProviderImpl session_shell_services_;

  fidl::BindingSet<fuchsia::modular::SessionShellContext>
      session_shell_context_bindings_;

  fuchsia::auth::TokenManagerPtr agent_token_manager_;
  fuchsia::modular::internal::SessionContextPtr session_context_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> cloud_provider_app_;
  fuchsia::ledger::cloud::firestore::FactoryPtr cloud_provider_factory_;
  std::unique_ptr<AppClient<fuchsia::ledger::internal::LedgerController>>
      ledger_app_;
  fuchsia::ledger::internal::LedgerRepositoryFactoryPtr
      ledger_repository_factory_;
  fuchsia::ledger::internal::LedgerRepositoryPtr ledger_repository_;
  std::unique_ptr<LedgerClient> ledger_client_;
  // Provides services to the Ledger
  component::ServiceProviderImpl ledger_service_provider_;

  std::unique_ptr<Environment> session_environment_;

  fuchsia::modular::auth::AccountPtr account_;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> context_engine_app_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> discovermgr_app_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> module_resolver_app_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> session_shell_app_;
  std::unique_ptr<ViewHost> session_shell_view_host_;

  std::unique_ptr<EntityProviderRunner> entity_provider_runner_;
  std::unique_ptr<ModuleFacetReader> module_facet_reader_;

  std::unique_ptr<SessionStorage> session_storage_;
  AsyncHolder<StoryProviderImpl> story_provider_impl_;
  std::unique_ptr<MessageQueueManager> message_queue_manager_;
  std::unique_ptr<AgentRunnerStorage> agent_runner_storage_;
  AsyncHolder<AgentRunner> agent_runner_;
  std::string device_name_;

  std::unique_ptr<StoryCommandExecutor> story_command_executor_;
  std::unique_ptr<PuppetMasterImpl> puppet_master_impl_;

  std::unique_ptr<SessionCtl> session_ctl_;

  // Services we provide to |context_engine_app_|.
  component::ServiceProviderImpl context_engine_ns_services_;

  // These component contexts are supplied to:
  // - the user intelligence provider so it can run agents and create message
  //   queues
  // - |context_engine_app_| so it can resolve entity references
  // - |modular resolver_service_| so it can resolve entity references
  std::unique_ptr<fidl::BindingSet<fuchsia::modular::ComponentContext,
                                   std::unique_ptr<ComponentContextImpl>>>
      maxwell_component_context_bindings_;

  std::unique_ptr<UserIntelligenceProviderImpl>
      user_intelligence_provider_impl_;

  // Services we provide to the module resolver's namespace.
  component::ServiceProviderImpl module_resolver_ns_services_;
  fuchsia::modular::ModuleResolverPtr module_resolver_service_;

  // Services we provide to the discovermgr's namespace.
  component::ServiceProviderImpl discovermgr_ns_services_;
  fuchsia::app::discover::DiscoverRegistryPtr discover_registry_service_;

  class PresentationProviderImpl;
  std::unique_ptr<PresentationProviderImpl> presentation_provider_impl_;

  std::unique_ptr<FocusHandler> focus_handler_;
  std::unique_ptr<VisibleStoriesHandler> visible_stories_handler_;

  // Component context given to session shell so that it can run agents and
  // create message queues.
  std::unique_ptr<ComponentContextImpl> session_shell_component_context_impl_;

  // Given to the session shell so it can store its own data. These data are
  // shared between all session shells (so it's not private to the session shell
  // *app*).
  std::unique_ptr<StoryStorage> session_shell_storage_;
  fidl::BindingSet<fuchsia::modular::Link, std::unique_ptr<LinkImpl>>
      session_shell_link_bindings_;

  // Holds the actions scheduled by calls to the AtEnd() method.
  std::vector<fit::function<void(fit::function<void()>)>> at_end_;

  // Holds the done callback of Terminate() while the at_end_ actions are being
  // executed. We can rely on Terminate() only being called once. (And if not,
  // this could simply be made a vector as usual.)
  fit::function<void()> at_end_done_;

  // The service provider used to connect to services advertised by the
  // clipboard agent.
  fuchsia::sys::ServiceProviderPtr services_from_clipboard_agent_;

  // The agent controller used to control the clipboard agent.
  fuchsia::modular::AgentControllerPtr clipboard_agent_controller_;

  class SwapSessionShellOperation;

  OperationQueue operation_queue_;

  // Part of Initialize() that is deferred until the first environment service
  // request is received from the session shell, in order to accelerate the
  // startup of session shell.
  fit::function<void()> finish_initialization_{[] {}};

  // Set to |true| when sessionmgr starts its terminating sequence;  this flag
  // can be used to determine whether to reject vending FIDL services.
  bool terminating_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionmgrImpl);

  fxl::WeakPtrFactory<SessionmgrImpl> weak_ptr_factory_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_SESSIONMGR_IMPL_H_
