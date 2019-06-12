// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Story service is the context in which a story executes. It
// starts modules and provides them with a handle to itself, so they
// can start more modules. It also serves as the factory for
// fuchsia::modular::Link instances, which are used to share data between
// modules.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/scenic/snapshot/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <src/lib/fxl/macros.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "peridot/bin/sessionmgr/puppet_master/command_runners/operation_calls/add_mod_call.h"
#include "peridot/bin/sessionmgr/storage/session_storage.h"
#include "peridot/bin/sessionmgr/story/model/story_mutator.h"
#include "peridot/bin/sessionmgr/story/model/story_observer.h"
#include "peridot/bin/sessionmgr/story_runner/link_impl.h"
#include "peridot/bin/sessionmgr/story_runner/ongoing_activity_impl.h"
#include "peridot/bin/sessionmgr/story_runner/story_shell_context_impl.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/environment.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

class ModuleContextImpl;
class ModuleControllerImpl;
class StoryModelMutator;
class StoryProviderImpl;
class StoryStorage;
class StoryVisibilitySystem;

// The story runner, which holds all the links and runs all the modules as well
// as the story shell. It also implements the StoryController service to give
// clients control over the story.
class StoryControllerImpl : fuchsia::modular::StoryController {
 public:
  StoryControllerImpl(SessionStorage* session_storage,
                      StoryStorage* story_storage,
                      std::unique_ptr<StoryMutator> story_mutator,
                      std::unique_ptr<StoryObserver> story_observer,
                      StoryVisibilitySystem* story_visibility_system,
                      StoryProviderImpl* story_provider_impl);
  ~StoryControllerImpl() override;

  // Called by StoryProviderImpl.
  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::StoryController> request);

  // Called by StoryProviderImpl.
  bool IsRunning();

  // Called by StoryProviderImpl.
  //
  // Returns a list of the ongoing activities in this story.
  fidl::VectorPtr<fuchsia::modular::OngoingActivityType> GetOngoingActivities();

  void Sync(fit::function<void()> done);

  // Called by ModuleControllerImpl and ModuleContextImpl.
  void FocusModule(const std::vector<std::string>& module_path);

  // Called by ModuleControllerImpl.
  void DefocusModule(const std::vector<std::string>& module_path);

  // Called by ModuleControllerImpl.
  void StopModule(const std::vector<std::string>& module_path,
                  fit::function<void()> done);

  // Called by ModuleControllerImpl.
  //
  // Releases ownership of |controller| and cleans up any related internal
  // storage. It is the caller's responsibility to delete |controller|.
  void ReleaseModule(ModuleControllerImpl* module_controller_impl);

  // Called by ModuleContextImpl.
  fidl::StringPtr GetStoryId() const;

  // Called by ModuleContextImpl.
  void RequestStoryFocus();

  // Called by ModuleContextImpl.
  void ConnectLinkPath(fuchsia::modular::LinkPathPtr link_path,
                       fidl::InterfaceRequest<fuchsia::modular::Link> request);

  // Called by ModuleContextImpl.
  fuchsia::modular::LinkPathPtr GetLinkPathForParameterName(
      const std::vector<std::string>& module_path, std::string name);

  // Called by ModuleContextImpl.
  void EmbedModule(
      AddModParams add_mod_params,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController>
          module_controller_request,
      fuchsia::ui::views::ViewToken view_token,
      fit::function<void(fuchsia::modular::StartModuleStatus)> callback);

  // Called by ModuleContextImpl.
  void AddModuleToStory(
      AddModParams add_mod_params,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController>
          module_controller_request,
      fit::function<void(fuchsia::modular::StartModuleStatus)> callback);

  // Stops the module at |module_path| in response to a call to
  // |ModuleContext.RemoveSelfFromStory|.
  void RemoveModuleFromStory(const std::vector<std::string>& module_path);

  // Called by ModuleContextImpl.
  void StartOngoingActivity(
      const fuchsia::modular::OngoingActivityType ongoing_activity_type,
      fidl::InterfaceRequest<fuchsia::modular::OngoingActivity> request);

  // Called by ModuleContextImpl.
  void CreateEntity(
      std::string type, fuchsia::mem::Buffer data,
      fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request,
      fit::function<void(std::string /* entity_reference */)> callback);

  // Stops the story as part of a story provider operation. The story provider
  // can indicate whether this is part of an operation where all stories are
  // stopped at once in order to stop the session shell, indicated by bulk being
  // true. Happens at logout or when session shells are swapped. In that
  // situation, DetachView() is not called for this story.
  void StopBulk(bool bulk, StopCallback done);

 private:
  // Operations implemented here.
  class AddIntentCall;
  class DefocusCall;
  class FocusCall;
  class KillModuleCall;
  class LaunchModuleCall;
  class LaunchModuleInShellCall;
  class OnModuleDataUpdatedCall;
  class ResolveParameterCall;
  class StartCall;
  class StopCall;
  class StopModuleCall;
  class StopModuleAndStoryIfEmptyCall;
  class StartSnapshotLoaderCall;
  class UpdateSnapshotCall;

  // For each *running* Module in the Story, there is one RunningModInfo.
  struct RunningModInfo {
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
    fuchsia::ui::views::ViewHolderToken module_pending_view_holder_token;
  };

  // A module's story shell-related information that we pend until we are able
  // to pass it off to the story shell.
  struct PendingViewForStoryShell {
    std::vector<std::string> module_path;
    fuchsia::modular::ModuleManifestPtr module_manifest;
    fuchsia::modular::SurfaceRelationPtr surface_relation;
    fuchsia::modular::ModuleSource module_source;
    fuchsia::ui::views::ViewHolderToken view_holder_token;
  };

  // |StoryController|
  void Stop(StopCallback done) override;
  void GetInfo(GetInfoCallback callback) override;
  void RequestStart() override;
  void TakeAndLoadSnapshot(fuchsia::ui::views::ViewToken view_token,
                           TakeAndLoadSnapshotCallback done) override;
  void Watch(
      fidl::InterfaceHandle<fuchsia::modular::StoryWatcher> watcher) override;
  void GetActiveModules(GetActiveModulesCallback callback) override;
  void GetModules(GetModulesCallback callback) override;
  void GetModuleController(
      std::vector<std::string> module_path,
      fidl::InterfaceRequest<fuchsia::modular::ModuleController> request)
      override;
  void GetLink(fuchsia::modular::LinkPath link_path,
               fidl::InterfaceRequest<fuchsia::modular::Link> request) override;

  // Communicates with SessionShell.
  void StartStoryShell();
  void DetachView(fit::function<void()> done);

  // Called whenever |story_storage_| sees an updated ModuleData from another
  // device.
  void OnModuleDataUpdated(fuchsia::modular::ModuleData module_data);

  // Misc internal helpers.
  void SetRuntimeState(fuchsia::modular::StoryState new_state);
  void NotifyStoryWatchers(
      const fuchsia::modular::storymodel::StoryModel& model);
  void NotifyOneStoryWatcher(
      const fuchsia::modular::storymodel::StoryModel& model,
      fuchsia::modular::StoryWatcher* watcher);
  void ProcessPendingStoryShellViews();
  std::set<fuchsia::modular::LinkPath> GetActiveLinksInternal();

  bool IsExternalModule(const std::vector<std::string>& module_path);

  // Handles SessionShell OnModuleFocused event that indicates whether or not a
  // surface was focused.
  void OnSurfaceFocused(fidl::StringPtr surface_id);

  // Finds the active RunningModInfo for a module at the given module path. May
  // return nullptr if the module at the path is not running, regardless of
  // whether a module at that path is known to the story.
  RunningModInfo* FindRunningModInfo(
      const std::vector<std::string>& module_path);

  // Finds the active RunningModInfo for the story shell anchor of a module
  // with the given |running_mod_info|. The anchor is the closest ancestor
  // module of the given module that is not embedded and actually known to the
  // story shell. This requires that it must be running, otherwise it cannot be
  // connected to the story shell. May return nullptr if the anchor module, or
  // any intermediate module, is not running, regardless of whether a module at
  // such path is known to the story.
  RunningModInfo* FindAnchor(RunningModInfo* running_mod_info);

  // The ID of the story, copied from |story_observer_| for convenience in
  // transitioning clients.  TODO(thatguy): Remove users of this in favor of
  // reading from the |story_observer_| directly.
  const fidl::StringPtr story_id_;

  StoryProviderImpl* const story_provider_impl_;  // Not owned.
  SessionStorage* const session_storage_;         // Not owned.
  StoryStorage* const story_storage_;             // Not owned.

  std::unique_ptr<StoryMutator> story_mutator_;
  std::unique_ptr<StoryObserver> story_observer_;
  StoryVisibilitySystem* const story_visibility_system_;  // Not owned.

  // Implements the primary service provided here:
  // fuchsia::modular::StoryController.
  fidl::BindingSet<fuchsia::modular::StoryController> bindings_;

  // Watcher for various aspects of the story.
  fidl::InterfacePtrSet<fuchsia::modular::StoryWatcher> watchers_;

  // Everything for the story shell. Relationships between modules are conveyed
  // to the story shell using their instance IDs.
  std::unique_ptr<AsyncHolderBase> story_shell_holder_;
  fuchsia::modular::StoryShellPtr story_shell_;

  StoryShellContextImpl story_shell_context_impl_;

  // The module instances (identified by their serialized module paths) already
  // known to story shell. Does not include modules whose views are pending and
  // not yet sent to story shell.
  std::set<fidl::StringPtr> connected_views_;

  // Since story shell cannot display views whose parents are not yet displayed,
  // |pending_story_shell_views_| holds the view of a non-embedded running
  // module (identified by its serialized module path) until its parent is
  // connected to story shell.
  std::map<std::string, PendingViewForStoryShell> pending_story_shell_views_;

  std::vector<RunningModInfo> running_mod_infos_;

  // The second ingredient of a story: Links. They connect Modules.
  fidl::BindingSet<Link, std::unique_ptr<LinkImpl>> link_impls_;

  // This is the source of truth on which activities are currently ongoing in
  // the story's modules.
  fidl::BindingSet<fuchsia::modular::OngoingActivity,
                   std::unique_ptr<OngoingActivityImpl>>
      ongoing_activities_;

  // Used to load snapshots.
  fuchsia::scenic::snapshot::LoaderPtr snapshot_loader_;

  // A collection of services, scoped to this Story, for use by intelligent
  // Modules.
  fuchsia::modular::IntelligenceServicesPtr intelligence_services_;

  // Asynchronous operations are sequenced in a queue.
  OperationQueue operation_queue_;

  fxl::WeakPtrFactory<StoryControllerImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerImpl);
};

// NOTE: This is only exposed publicly for testing.
bool ShouldRestartModuleForNewIntent(
    const fuchsia::modular::Intent& old_intent,
    const fuchsia::modular::Intent& new_intent);

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
