// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_SESSIONMGR_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_SESSIONMGR_IMPL_H_

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fit/function.h>
#include <lib/sys/inspect/cpp/component.h>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/sessionmgr/argv_injecting_launcher.h"
#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_launcher.h"
#include "src/modular/bin/sessionmgr/entity_provider_runner/entity_provider_runner.h"
#include "src/modular/bin/sessionmgr/startup_agent_launcher.h"
#include "src/modular/lib/common/async_holder.h"
#include "src/modular/lib/deprecated_service_provider/service_provider_impl.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/environment.h"
#include "src/modular/lib/fidl/view_host.h"
#include "src/modular/lib/scoped_tmpfs/scoped_tmpfs.h"

namespace modular {

class AgentRunner;
class ComponentContextImpl;
class DeviceMapImpl;
class FocusHandler;
class LedgerClient;
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
  SessionmgrImpl(sys::ComponentContext* component_context,
                 fuchsia::modular::session::SessionmgrConfig config, inspect::Node object);
  ~SessionmgrImpl() override;

  // |AppDriver| calls this.
  void Terminate(fit::function<void()> callback);

 private:
  // |Sessionmgr|
  void Initialize(std::string session_id, fuchsia::modular::AppConfig session_shell_config,
                  fuchsia::modular::AppConfig story_shell_config,
                  bool use_session_shell_for_story_shell_factory,
                  fidl::InterfaceHandle<fuchsia::modular::internal::SessionContext> session_context,
                  fuchsia::ui::views::ViewToken view_token) override;

  // Completes any deferred initialization steps from Initialize().
  void MaybeFinishInitialization();

  // |Sessionmgr|
  void SwapSessionShell(fuchsia::modular::AppConfig session_shell_config,
                        SwapSessionShellCallback callback) override;

  // Sequence of Initialize() broken up into steps for clarity.
  void InitializeSessionEnvironment(std::string session_id);
  void InitializeLedger();
  void InitializeAgentRunner();
  void InitializeIntlPropertyProvider();
  void InitializeDeviceMap();
  void InitializeModular(const fidl::StringPtr& session_shell_url,
                         fuchsia::modular::AppConfig story_shell_config,
                         bool use_session_shell_for_story_shell_factory);
  void InitializeSessionShell(fuchsia::modular::AppConfig session_shell_config,
                              fuchsia::ui::views::ViewToken view_token);

  void RunSessionShell(fuchsia::modular::AppConfig session_shell_config);
  // This is a termination sequence that may be used with |OnTerminate()|, but also
  // may be executed to terminate the currently running session shell.
  void TerminateSessionShell(fit::function<void()> done);

  // Returns the file descriptor that backs the ledger repository directory for
  // the user.
  zx::channel GetLedgerRepositoryDirectory();

  // |fuchsia::modular::SessionShellContext|
  void GetComponentContext(
      fidl::InterfaceRequest<fuchsia::modular::ComponentContext> request) override;
  void GetFocusController(
      fidl::InterfaceRequest<fuchsia::modular::FocusController> request) override;
  void GetFocusProvider(fidl::InterfaceRequest<fuchsia::modular::FocusProvider> request) override;
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override;
  void GetStoryProvider(fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) override;
  void Logout() override;
  void Restart() override;
  void Shutdown();

  // |EntityProviderLauncher|
  void ConnectToEntityProvider(
      const std::string& agent_url,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request,
      fidl::InterfaceRequest<fuchsia::modular::AgentController> agent_controller_request) override;

  // |EntityProviderLauncher|
  void ConnectToStoryEntityProvider(
      const std::string& story_id,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider> entity_provider_request) override;

  // Called during initialization. Schedules the given action to be executed
  // during termination. This allows to create something like an asynchronous
  // destructor at initialization time. The sequence of actions thus scheduled
  // is executed in reverse in Terminate().
  //
  // The OnTerminate() calls for a field should happen next to the calls that
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
  void OnTerminate(fit::function<void(fit::function<void()>)> action);

  // Recursively execute the termiation steps scheduled by OnTerminate(). The
  // execution steps are stored in on_terminate_cbs_.
  void TerminateRecurse(int i);

  void ConnectSessionShellToStoryProvider();

  // The device-local unique identifier for this session. The uniqueness
  // is enforced by basemgr which vends sessions.
  std::string session_id_;

  // The context in which the Sessionmgr was launched
  sys::ComponentContext* const sessionmgr_context_;

  // The launcher from the context in which the Sessionmgr was launched. The Sessionmgr will use
  // this launcher to launch other dependent services such as ledger, cloud provider, etc.
  fuchsia::sys::LauncherPtr sessionmgr_context_launcher_;

  // The Sessionmgr creates a new environment as a child of its component context's environment.
  // Story shells and mods are launched within the |session_environment_|. Other services are
  // launched outside of the |session_environment_| (and in the sessionmgr_context_ environment).
  // **NOTE: Agents, logically, should be in the |session_environment_| as well, but for legacy
  // reasons due to hard dependencies on a /data path that does not have "session" uniqueness,
  // Agents are launched in the |sessionmgr_context_| environment as well. Since Modular services
  // like this will only support one session anyway, this is acceptable for now, and will be
  // resolved in Session Framework (replacing Modular).
  std::unique_ptr<Environment> session_environment_;

  fuchsia::modular::session::SessionmgrConfig config_;

  inspect::Node inspect_root_node_;

  std::unique_ptr<scoped_tmpfs::ScopedTmpFS> memfs_for_ledger_;

  fidl::BindingSet<fuchsia::modular::internal::Sessionmgr> bindings_;
  component::ServiceProviderImpl session_shell_services_;

  fidl::BindingSet<fuchsia::modular::SessionShellContext> session_shell_context_bindings_;

  fuchsia::modular::internal::SessionContextPtr session_context_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> cloud_provider_app_;
  std::unique_ptr<AppClient<fuchsia::ledger::internal::LedgerController>> ledger_app_;
  fuchsia::ledger::internal::LedgerRepositoryFactoryPtr ledger_repository_factory_;
  fuchsia::ledger::internal::LedgerRepositoryPtr ledger_repository_;
  std::unique_ptr<LedgerClient> ledger_client_;
  // Provides services to the Ledger
  component::ServiceProviderImpl ledger_service_provider_;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> session_shell_app_;
  std::unique_ptr<ViewHost> session_shell_view_host_;

  std::unique_ptr<EntityProviderRunner> entity_provider_runner_;

  std::unique_ptr<SessionStorage> session_storage_;
  AsyncHolder<StoryProviderImpl> story_provider_impl_;

  std::unique_ptr<ArgvInjectingLauncher> agent_runner_launcher_;
  AsyncHolder<AgentRunner> agent_runner_;

  std::unique_ptr<StoryCommandExecutor> story_command_executor_;
  std::unique_ptr<PuppetMasterImpl> puppet_master_impl_;

  std::unique_ptr<SessionCtl> session_ctl_;

  std::unique_ptr<StartupAgentLauncher> startup_agent_launcher_;

  class PresentationProviderImpl;
  std::unique_ptr<PresentationProviderImpl> presentation_provider_impl_;

  std::unique_ptr<FocusHandler> focus_handler_;

  // Component context given to session shell so that it can run agents.
  std::unique_ptr<ComponentContextImpl> session_shell_component_context_impl_;

  // Given to the session shell so it can store its own data. These data are
  // shared between all session shells (so it's not private to the session shell
  // *app*).
  std::unique_ptr<StoryStorage> session_shell_storage_;

  // Holds the actions scheduled by calls to the OnTerminate() method.
  std::vector<fit::function<void(fit::function<void()>)>> on_terminate_cbs_;

  // Holds the done callback of Terminate() while the on_terminate_cbs_ actions are being
  // executed.
  fit::function<void()> terminate_done_;

  class SwapSessionShellOperation;

  OperationQueue operation_queue_;

  // Part of Initialize() that is deferred until the first environment service
  // request is received from the session shell, in order to accelerate the
  // startup of session shell.
  fit::function<void()> deferred_initialization_cb_;

  // Set to |true| when sessionmgr starts its terminating sequence;  this flag
  // can be used to determine whether to reject vending FIDL services.
  bool terminating_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionmgrImpl);

  fxl::WeakPtrFactory<SessionmgrImpl> weak_ptr_factory_;
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_SESSIONMGR_IMPL_H_
