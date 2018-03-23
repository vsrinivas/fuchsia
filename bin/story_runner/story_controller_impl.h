// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Story service is the context in which a story executes. It
// starts modules and provides them with a handle to itself, so they
// can start more modules. It also serves as the factory for Link
// instances, which are used to share data between modules.

#ifndef PERIDOT_BIN_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
#define PERIDOT_BIN_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/ledger.h>
#include <fuchsia/cpp/modular.h>
#include <fuchsia/cpp/views_v1_token.h>
#include "lib/async/cpp/operation.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/story_runner/link_impl.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/scope.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

class ChainImpl;
class ModuleControllerImpl;
class ModuleContextImpl;
class StoryProviderImpl;

constexpr char kRootLink[] = "root";
constexpr char kRootModuleName[] = "root";

// The story runner, which holds all the links and runs all the modules as well
// as the story shell. It also implements the StoryController service to give
// clients control over the story.
class StoryControllerImpl : PageClient, StoryController, StoryContext {
 public:
  StoryControllerImpl(fidl::StringPtr story_id,
                      LedgerClient* ledger_client,
                      LedgerPageId story_page_id,
                      StoryProviderImpl* story_provider_impl);
  ~StoryControllerImpl() override;

  // Called by StoryProviderImpl.
  void Connect(fidl::InterfaceRequest<StoryController> request);

  // Called by StoryProviderImpl.
  bool IsRunning();

  // Called by StoryProviderImpl.
  //
  // A variant of Stop() that stops the story because the story is being
  // deleted. The StoryControllerImpl instance is deleted by StoryProviderImpl
  // and the story data are deleted from the ledger once the done callback is
  // invoked.
  //
  // No further operations invoked after this one are executed. (The Operation
  // accomplishes this by not calling Done() and instead invoking its callback
  // directly from Run(), such that the OperationQueue stays blocked on it until
  // it gets deleted.)
  void StopForDelete(const std::function<void()>& done);

  // Called by StoryProviderImpl.
  void StopForTeardown(const std::function<void()>& done);

  // Called by StoryProviderImpl.
  void AddForCreate(fidl::StringPtr module_name,
                    fidl::StringPtr module_url,
                    fidl::StringPtr link_name,
                    CreateLinkInfoPtr create_link_info,
                    const std::function<void()>& done);

  // Called by StoryProviderImpl.
  StoryState GetStoryState() const;
  void Sync(const std::function<void()>& done);

  // Called by ModuleControllerImpl and ModuleContextImpl.
  void FocusModule(const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // Called by ModuleControllerImpl.
  void DefocusModule(const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // Called by ModuleControllerImpl.
  void StopModule(const fidl::VectorPtr<fidl::StringPtr>& module_path,
                  const std::function<void()>& done);

  // Called by ModuleControllerImpl.
  void OnModuleStateChange(const fidl::VectorPtr<fidl::StringPtr>& module_path,
                           ModuleState state);

  // Called by ModuleControllerImpl.
  //
  // Releases ownership of |controller|, which deletes itself after return.
  void ReleaseModule(ModuleControllerImpl* module_controller_impl);

  // Called by ModuleContextImpl.
  fidl::StringPtr GetStoryId() const;

  // Called by ModuleContextImpl.
  void RequestStoryFocus();

  // Called by ModuleContextImpl.
  void ConnectLinkPath(LinkPathPtr link_path,
                       LinkImpl::ConnectionType connection_type,
                       fidl::InterfaceRequest<Link> request);

  // Called by ModuleContextImpl.
  LinkPathPtr GetLinkPathForChainKey(
      const fidl::VectorPtr<fidl::StringPtr>& module_path,
      fidl::StringPtr key);

  // Called by ModuleContextImpl.
  // TODO(thatguy): Remove this entirely once all Modules use StartModule.
  // MI4-739
  void StartModuleDeprecated(
      const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
      fidl::StringPtr module_name,
      fidl::StringPtr module_url,
      fidl::StringPtr link_name,
      const modular::ModuleManifestPtr manifest,
      CreateChainInfoPtr create_chain_info,
      fidl::InterfaceRequest<component::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller_request,
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
      ModuleSource module_source);

  // Called by ModuleContextImpl and AddModule.
  // TODO(thatguy): Remove this entirely once all Modules use StartModule.
  // MI4-739
  void StartModuleInShellDeprecated(
      const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
      fidl::StringPtr module_name,
      fidl::StringPtr module_url,
      fidl::StringPtr link_name,
      const modular::ModuleManifestPtr manifest,
      CreateChainInfoPtr create_chain_info,
      fidl::InterfaceRequest<component::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller_request,
      SurfaceRelationPtr surface_relation,
      bool focus,
      ModuleSource module_source);

  // Called by ModuleContextImpl.
  void EmbedModule(
      const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
      fidl::StringPtr module_name,
      DaisyPtr daisy,
      fidl::InterfaceRequest<component::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller_request,
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
      ModuleSource module_source,
      std::function<void(StartModuleStatus)> callback);

  // Called by ModuleContextImpl.
  void StartModule(
      const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
      fidl::StringPtr module_name,
      DaisyPtr daisy,
      fidl::InterfaceRequest<component::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller_request,
      SurfaceRelationPtr surface_relation,
      ModuleSource module_source,
      std::function<void(StartModuleStatus)> callback);

  // Called by ModuleContextImpl.
  void StartContainerInShell(
      const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
      fidl::StringPtr name,
      SurfaceRelationPtr parent_relation,
      fidl::VectorPtr<ContainerLayout> layout,
      fidl::VectorPtr<ContainerRelationEntry> relationships,
      fidl::VectorPtr<ContainerNodePtr> nodes);

 private:
  class ModuleWatcherImpl;

  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // |StoryController|
  void GetInfo(GetInfoCallback callback) override;
  void SetInfoExtra(fidl::StringPtr name,
                    fidl::StringPtr value,
                    SetInfoExtraCallback callback) override;
  void Start(
      fidl::InterfaceRequest<views_v1_token::ViewOwner> request) override;
  void Stop(StopCallback done) override;
  void Watch(fidl::InterfaceHandle<StoryWatcher> watcher) override;
  void AddModuleDeprecated(fidl::VectorPtr<fidl::StringPtr> module_path,
                           fidl::StringPtr module_name,
                           fidl::StringPtr module_url,
                           fidl::StringPtr link_name,
                           SurfaceRelationPtr surface_relation) override;
  void GetActiveModules(fidl::InterfaceHandle<StoryModulesWatcher> watcher,
                        GetActiveModulesCallback callback) override;
  void GetModules(GetModulesCallback callback) override;
  void GetModuleController(
      fidl::VectorPtr<fidl::StringPtr> module_path,
      fidl::InterfaceRequest<ModuleController> request) override;
  void GetActiveLinks(fidl::InterfaceHandle<StoryLinksWatcher> watcher,
                      GetActiveLinksCallback callback) override;
  void GetLink(fidl::VectorPtr<fidl::StringPtr> module_path,
               fidl::StringPtr name,
               fidl::InterfaceRequest<Link> request) override;
  void AddModule(fidl::VectorPtr<fidl::StringPtr> module_path,
                 fidl::StringPtr module_name,
                 Daisy daisy,
                 SurfaceRelationPtr surface_relation) override;

  // Phases of Start() broken out into separate methods.
  void StartStoryShell(
      fidl::InterfaceRequest<views_v1_token::ViewOwner> request);

  // Misc internal helpers.
  void NotifyStateChange();
  void DisposeLink(LinkImpl* link);
  void AddModuleWatcher(ModuleControllerPtr module_controller,
                        const fidl::VectorPtr<fidl::StringPtr>& module_path);
  void UpdateStoryState(ModuleState state);
  void ProcessPendingViews();

  bool IsExternalModule(const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // The ID of the story, its state and the context to obtain it from and
  // persist it to.
  const fidl::StringPtr story_id_;

  // This is the canonical source for state. The value in the ledger is just a
  // write-behind copy of this value.
  StoryState state_{StoryState::INITIAL};

  // Story state is determined by external module state, but only until the
  // story gets stopped or deleted. This flag blocks processing of state
  // notifications from modules while the story winds down.
  bool track_root_module_state_{true};

  StoryProviderImpl* const story_provider_impl_;

  LedgerClient* const ledger_client_;
  const LedgerPageId story_page_id_;

  // The scope in which the modules within this story run.
  Scope story_scope_;

  // Implements the primary service provided here: StoryController.
  fidl::BindingSet<StoryController> bindings_;

  // Watcher for various aspects of the story.
  fidl::InterfacePtrSet<StoryWatcher> watchers_;
  fidl::InterfacePtrSet<StoryModulesWatcher> modules_watchers_;
  fidl::InterfacePtrSet<StoryLinksWatcher> links_watchers_;

  // Everything for the story shell. Relationships between modules are conveyed
  // to the story shell using their instance IDs.
  std::unique_ptr<AppClient<Lifecycle>> story_shell_app_;
  StoryShellPtr story_shell_;
  fidl::Binding<StoryContext> story_context_binding_;

  // The module instances (identified by their serialized module paths) already
  // known to story shell. Does not include modules whose views are pending and
  // not yet sent to story shell.
  std::set<fidl::StringPtr> connected_views_;

  // Holds the view of a non-embedded running module (identified by its
  // serialized module path) until its parent is connected to story shell. Story
  // shell cannot display views whose parents are not yet displayed.
  std::map<
      fidl::StringPtr,
      std::pair<fidl::VectorPtr<fidl::StringPtr>, views_v1_token::ViewOwnerPtr>>
      pending_views_;

  // The first ingredient of a story: Modules. For each Module in the Story,
  // there is one Connection to it.
  struct Connection {
    ModuleDataPtr module_data;
    std::unique_ptr<ModuleContextImpl> module_context_impl;
    std::unique_ptr<ModuleControllerImpl> module_controller_impl;
  };
  std::vector<Connection> connections_;

  // Finds the active connection for a module at the given module path. May
  // return nullptr if the module at the path is not running, regardless of
  // whether a module at that path is known to the story.
  Connection* FindConnection(
      const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // Finds the active connection for the story shell anchor of a module with the
  // given connection. The anchor is the closest ancestor module of the given
  // module that is not embedded and actually known to the story shell. This
  // requires that it must be running, otherwise it cannot be connected to the
  // story shell. May return nullptr if the anchor module, or any intermediate
  // module, is not running, regardless of whether a module at such path is
  // known to the story.
  Connection* FindAnchor(Connection* connection);

  // The magic ingredient of a story: Chains. They group Links.
  std::vector<std::unique_ptr<ChainImpl>> chains_;

  // The second ingredient of a story: Links. They connect Modules.
  std::vector<std::unique_ptr<LinkImpl>> links_;

  // Module state is used to inform Story state (see OnModuleStateChange() and
  // MaybeUpdateStoryState()). We keep track of the first Module to start in
  // this Story as a proxy 'root' Module.
  fidl::VectorPtr<fidl::StringPtr> first_module_path_;

  // A dummy service that allows applications that can run both as modules in a
  // story and standalone from the shell to determine whether they are in a
  // story. See story_marker.fidl for more details.
  class StoryMarkerImpl;
  std::unique_ptr<StoryMarkerImpl> story_marker_impl_;

  // A collection of services, scoped to this Story, for use by intelligent
  // Modules.
  IntelligenceServicesPtr intelligence_services_;

  // Asynchronous operations are sequenced in a queue.
  OperationQueue operation_queue_;

  // Operations implemented here.
  class LaunchModuleCall;
  class KillModuleCall;
  class StartModuleCall;
  class StartModuleInShellCall;
  class StartContainerInShellCall;
  class AddModuleCall;
  class AddForCreateCall;
  class StopCall;
  class StopModuleCall;
  class DeleteCall;
  class ConnectLinkCall;
  class InitializeChainCall;
  class StartCall;
  class LedgerNotificationCall;
  class FocusCall;
  class DefocusCall;
  class BlockingModuleDataWriteCall;
  class ResolveModulesCall;
  class AddDaisyCall;

  // A blocking module data write call blocks while waiting for some
  // notifications, which are received by the StoryControllerImpl instance.
  std::vector<std::pair<ModuleData, BlockingModuleDataWriteCall*>>
      blocked_operations_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
