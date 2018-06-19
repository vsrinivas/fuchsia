// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Story service is the context in which a story executes. It
// starts modules and provides them with a handle to itself, so they
// can start more modules. It also serves as the factory for
// fuchsia::modular::Link instances, which are used to share data between
// modules.

#ifndef PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
#define PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>

#include "lib/async/cpp/operation.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_ptr.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/user_runner/story_runner/link_impl.h"
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
class StoryStorage;

// The story runner, which holds all the links and runs all the modules as well
// as the story shell. It also implements the StoryController service to give
// clients control over the story.
class StoryControllerImpl : fuchsia::modular::StoryController,
                            fuchsia::modular::StoryContext {
 public:
  StoryControllerImpl(fidl::StringPtr story_id, StoryStorage* story_storage,
                      StoryProviderImpl* story_provider_impl);
  ~StoryControllerImpl() override;

  // Called by StoryProviderImpl.
  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::StoryController> request);

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
  fuchsia::modular::StoryState GetStoryState() const;
  void Sync(const std::function<void()>& done);

  // Called by ModuleControllerImpl and ModuleContextImpl.
  void FocusModule(const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // Called by ModuleControllerImpl.
  void DefocusModule(const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // Called by ModuleControllerImpl.
  void StopModule(const fidl::VectorPtr<fidl::StringPtr>& module_path,
                  const std::function<void()>& done);

  // Called by ModuleControllerImpl.
  //
  // Releases ownership of |controller| and cleans up any related internal
  // storage. It is the caller's responsibility to delete |controller|.
  void ReleaseModule(ModuleControllerImpl* controller);

  // Called by ModuleContextImpl.
  fidl::StringPtr GetStoryId() const;

  // Called by ModuleContextImpl.
  void RequestStoryFocus();

  // Called by ModuleContextImpl.
  void ConnectLinkPath(fuchsia::modular::LinkPathPtr link_path,
                       fidl::InterfaceRequest<fuchsia::modular::Link> request);

  // Called by ModuleContextImpl.
  fuchsia::modular::LinkPathPtr GetLinkPathForParameterName(
      const fidl::VectorPtr<fidl::StringPtr>& module_path, fidl::StringPtr key);

  // Called by ModuleContextImpl.
  void EmbedModule(
      const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
      fidl::StringPtr module_name, fuchsia::modular::IntentPtr intent,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController>
          module_controller_request,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      fuchsia::modular::ModuleSource module_source,
      std::function<void(fuchsia::modular::StartModuleStatus)> callback);

  // Called by ModuleContextImpl.
  void StartModule(
      const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
      fidl::StringPtr module_name, fuchsia::modular::IntentPtr intent,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController>
          module_controller_request,
      fuchsia::modular::SurfaceRelationPtr surface_relation,
      fuchsia::modular::ModuleSource module_source,
      std::function<void(fuchsia::modular::StartModuleStatus)> callback);

  // Called by ModuleContextImpl.
  void StartContainerInShell(
      const fidl::VectorPtr<fidl::StringPtr>& parent_module_path,
      fidl::StringPtr name,
      fuchsia::modular::SurfaceRelationPtr parent_relation,
      fidl::VectorPtr<fuchsia::modular::ContainerLayout> layout,
      fidl::VectorPtr<fuchsia::modular::ContainerRelationEntry> relationships,
      fidl::VectorPtr<fuchsia::modular::ContainerNodePtr> nodes);

  // |fuchsia::modular::StoryController| - public so that
  // fuchsia::modular::StoryProvider can call it
  void AddModule(
      fidl::VectorPtr<fidl::StringPtr> module_path, fidl::StringPtr module_name,
      fuchsia::modular::Intent intent,
      fuchsia::modular::SurfaceRelationPtr surface_relation) override;

  // Called by ModuleContextImpl.
  void Active();

 private:
  class ModuleWatcherImpl;

  // |StoryController|
  void GetInfo(GetInfoCallback callback) override;
  void Start(fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
                 request) override;
  void Stop(StopCallback done) override;
  void Watch(
      fidl::InterfaceHandle<fuchsia::modular::StoryWatcher> watcher) override;
  void GetActiveModules(
      fidl::InterfaceHandle<fuchsia::modular::StoryModulesWatcher> watcher,
      GetActiveModulesCallback callback) override;
  void GetModules(GetModulesCallback callback) override;
  void GetModuleController(
      fidl::VectorPtr<fidl::StringPtr> module_path,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController> request)
      override;
  void GetActiveLinks(
      fidl::InterfaceHandle<fuchsia::modular::StoryLinksWatcher> watcher,
      GetActiveLinksCallback callback) override;
  void GetLink(fidl::VectorPtr<fidl::StringPtr> module_path,
               fidl::StringPtr name,
               fidl::InterfaceRequest<fuchsia::modular::Link> request) override;

  // |fuchsia::modular::StoryContext|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override;
  void WatchVisualState(
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher)
      override;

  // Phases of Start() broken out into separate methods.
  void StartStoryShell(
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner> request);

  // Called whenever |story_storage_| sees an updated ModuleData from another
  // device.
  void OnModuleDataUpdated(fuchsia::modular::ModuleData module_data);

  // Misc internal helpers.
  void SetState(fuchsia::modular::StoryState new_state);
  void AddModuleWatcher(fuchsia::modular::ModuleControllerPtr module_controller,
                        const fidl::VectorPtr<fidl::StringPtr>& module_path);
  void UpdateStoryState(fuchsia::modular::ModuleState state);
  void ProcessPendingViews();
  std::set<fuchsia::modular::LinkPath> GetActiveLinksInternal();

  bool IsExternalModule(const fidl::VectorPtr<fidl::StringPtr>& module_path);

  // The ID of the story, its state and the context to obtain it from and
  // persist it to.
  const fidl::StringPtr story_id_;

  // This is the canonical source for state. The value in the ledger is just a
  // write-behind copy of this value.
  fuchsia::modular::StoryState state_{fuchsia::modular::StoryState::STOPPED};

  StoryProviderImpl* const story_provider_impl_;

  StoryStorage* const story_storage_;

  // The scope in which the modules within this story run.
  Scope story_scope_;

  // Implements the primary service provided here:
  // fuchsia::modular::StoryController.
  fidl::BindingSet<fuchsia::modular::StoryController> bindings_;

  // Watcher for various aspects of the story.
  fidl::InterfacePtrSet<fuchsia::modular::StoryWatcher> watchers_;
  fidl::InterfacePtrSet<fuchsia::modular::StoryModulesWatcher>
      modules_watchers_;
  fidl::InterfacePtrSet<fuchsia::modular::StoryLinksWatcher> links_watchers_;

  // Everything for the story shell. Relationships between modules are conveyed
  // to the story shell using their instance IDs.
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> story_shell_app_;
  fuchsia::modular::StoryShellPtr story_shell_;
  fidl::Binding<fuchsia::modular::StoryContext> story_context_binding_;

  // The module instances (identified by their serialized module paths) already
  // known to story shell. Does not include modules whose views are pending and
  // not yet sent to story shell.
  std::set<fidl::StringPtr> connected_views_;

  // Holds the view of a non-embedded running module (identified by its
  // serialized module path) until its parent is connected to story shell. Story
  // shell cannot display views whose parents are not yet displayed.
  struct PendingView {
    fidl::VectorPtr<fidl::StringPtr> module_path;
    fuchsia::modular::ModuleManifestPtr module_manifest;
    fuchsia::modular::SurfaceRelationPtr surface_relation;
    fuchsia::ui::views_v1_token::ViewOwnerPtr view_owner;
  };
  std::map<fidl::StringPtr, PendingView> pending_views_;

  // The first ingredient of a story: Modules. For each Module in the Story,
  // there is one Connection to it.
  struct Connection {
    // NOTE: |module_data| is a cached copy of what is stored in
    // |story_storage_|, the source of truth. It is updated in two
    // places:
    //
    // 1) In LaunchModuleCall (used by LaunchModuleInShellCall) in the case
    // that either a) the module isn't running yet or b) ModuleData.intent
    // differs from what is cached.
    //
    // 2) Indirectly from OnModuleDataUpdated(), which is called when another
    // device updates the Module by calling LaunchModuleInShellCall. However,
    // this only happens if the Module is EXTERNAL (it was not explicitly added
    // by another Module).
    //
    // TODO(thatguy): we should ensure that the local cached copy is always
    // up to date no matter what.
    fuchsia::modular::ModuleDataPtr module_data;
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
  // TODO(thatguy): Remove ChainImpl, and rename this to mod_parameter_maps_.
  std::vector<std::unique_ptr<ChainImpl>> chains_;

  // The second ingredient of a story: Links. They connect Modules.
  fidl::BindingSet<Link, std::unique_ptr<LinkImpl>> link_impls_;

  // A collection of services, scoped to this Story, for use by intelligent
  // Modules.
  fuchsia::modular::IntelligenceServicesPtr intelligence_services_;

  // Asynchronous operations are sequenced in a queue.
  OperationQueue operation_queue_;

  // Operations implemented here.
  class AddIntentCall;
  class ConnectLinkCall;
  class DefocusCall;
  class DeleteCall;
  class FocusCall;
  class InitializeChainCall;
  class KillModuleCall;
  class LaunchModuleCall;
  class LaunchModuleInShellCall;
  class OnModuleDataUpdatedCall;
  class ResolveModulesCall;
  class ResolveParameterCall;
  class StartCall;
  class StartContainerInShellCall;
  class StopCall;
  class StopModuleCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_USER_RUNNER_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
