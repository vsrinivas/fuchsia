// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The Story service is the context in which a story executes. It
// starts modules and provides them with a handle to itself, so they
// can start more modules.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/macros.h"
#include "src/modular/bin/sessionmgr/puppet_master/command_runners/operation_calls/add_mod_call.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"
#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/bin/sessionmgr/story_runner/module_context_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/module_controller_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/ongoing_activity_impl.h"
#include "src/modular/bin/sessionmgr/story_runner/story_shell_context_impl.h"
#include "src/modular/lib/async/cpp/operation.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/environment.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

namespace modular {

// StoryProviderImpl has a circular dependency on StoryControllerImpl.
class StoryProviderImpl;

// The story runner, which runs all the modules as well as the story shell.
// It also implements the StoryController service to give clients control over
// the story.
class StoryControllerImpl : fuchsia::modular::StoryController {
 public:
  StoryControllerImpl(std::string story_id, SessionStorage* session_storage,
                      StoryStorage* story_storage, StoryProviderImpl* story_provider_impl,
                      inspect::Node* story_inspect_node);
  ~StoryControllerImpl() override;

  // Called by StoryProviderImpl.
  void Connect(fidl::InterfaceRequest<fuchsia::modular::StoryController> request);

  // Called by StoryProviderImpl.
  bool IsRunning();

  void Sync(fit::function<void()> done);

  // Called by ModuleControllerImpl and ModuleContextImpl.
  void FocusModule(const std::vector<std::string>& module_path);

  // Called by ModuleControllerImpl.
  void DefocusModule(const std::vector<std::string>& module_path);

  // Called by ModuleControllerImpl.
  void DeleteModule(const std::vector<std::string>& module_path, fit::function<void()> done);

  // Called by ModuleContextImpl.
  fuchsia::modular::StoryState runtime_state() const { return runtime_state_; }

  // Stops the module at |module_path| in response to a call to
  // |ModuleContext.RemoveSelfFromStory|.
  void RemoveModuleFromStory(const std::vector<std::string>& module_path);

  // Tears down the story and optionally skips notifying the session shell
  // that the story view has gone away.
  void Teardown(bool skip_notifying_sessionshell, StopCallback done);

 private:
  // Operations implemented here.
  class AddIntentCall;
  class DefocusCall;
  class FocusCall;
  class TeardownModuleCall;
  class LaunchModuleCall;
  class LaunchModuleInShellCall;
  class OnModuleDataUpdatedCall;
  class ResolveParameterCall;
  class StartCall;
  class TeardownStoryCall;
  class DeleteModuleCall;
  class DeleteModuleAndTeardownStoryIfEmptyCall;

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

    // Token passed to the story shell for displaying non-embedded modules.
    // This is set when the module is launched, and moved into |view_connection| once
    // the module is connected to the story shell, or pended to be connected.
    // Only set for non-embedded modules.
    std::optional<fuchsia::ui::views::ViewHolderToken> pending_view_holder_token;

    // The module's view (surface ID and view token) that was connected to the story shell.
    // Only set for non-embedded, non-pending modules.
    std::optional<fuchsia::modular::ViewConnection> view_connection;

    // A reference to the module's view.
    std::optional<fuchsia::ui::views::ViewRef> view_ref;

    // Metadata for the module's surface that was connected to the story shell.
    // Only set for non-embedded, non-pending modules.
    std::optional<fuchsia::modular::SurfaceInfo2> surface_info;

    inspect::Node mod_inspect_node;
    inspect::StringProperty is_embedded_property;
    inspect::StringProperty is_deleted_property;
    inspect::StringProperty module_source_property;
    inspect::StringProperty module_intent_action_property;
    inspect::StringProperty module_intent_params_property;
    inspect::StringProperty module_surface_relation_arrangement;
    inspect::StringProperty module_surface_relation_dependency;
    inspect::DoubleProperty module_surface_relation_emphasis;
    inspect::StringProperty module_path_property;
    std::map<std::string, inspect::StringProperty> annotation_properties;

    // Helper for initializing inspect nodes and properties.
    void InitializeInspectProperties(StoryControllerImpl* const story_controller_impl);
    void UpdateInspectProperties();
  };

  // A module's story shell-related information that we pend until we are able
  // to pass it off to the story shell.
  struct PendingViewForStoryShell {
    std::vector<std::string> module_path;
    fuchsia::modular::ViewConnection view_connection;
    fuchsia::modular::SurfaceInfo2 surface_info;
  };

  // |StoryController|
  void Stop(StopCallback done) override;
  void GetInfo(GetInfoCallback callback) override;
  void GetInfo2(GetInfo2Callback callback) override;
  void RequestStart() override;
  void Watch(fidl::InterfaceHandle<fuchsia::modular::StoryWatcher> watcher) override;

  // |StoryController|
  void Annotate(std::vector<fuchsia::modular::Annotation> annotations,
                AnnotateCallback callback) override;

  // Communicates with SessionShell.
  void StartStoryShell();
  void DetachView(fit::function<void()> done);

  // Called whenever |story_storage_| sees an updated ModuleData from another
  // device.
  void OnModuleDataUpdated(fuchsia::modular::ModuleData module_data);

  // Misc internal helpers.
  void SetRuntimeState(fuchsia::modular::StoryState new_state);
  void NotifyStoryWatchers();
  void NotifyOneStoryWatcher(fuchsia::modular::StoryWatcher* watcher);
  void ProcessPendingStoryShellViews();

  bool IsExternalModule(const std::vector<std::string>& module_path);

  // Deletes the entry for this module_path from running_mod_infos_.
  void EraseRunningModInfo(std::vector<std::string> module_path);

  // Handles SessionShell OnModuleFocused event that indicates whether or not a
  // surface was focused.
  void OnSurfaceFocused(fidl::StringPtr surface_id);

  // Finds the active RunningModInfo for a module at the given module path. May
  // return nullptr if the module at the path is not running, regardless of
  // whether a module at that path is known to the story.
  RunningModInfo* FindRunningModInfo(const std::vector<std::string>& module_path);

  // Finds the active RunningModInfo for the story shell anchor of a module
  // with the given |running_mod_info|. The anchor is the closest ancestor
  // module of the given module that is not embedded and actually known to the
  // story shell. This requires that it must be running, otherwise it cannot be
  // connected to the story shell. May return nullptr if the anchor module, or
  // any intermediate module, is not running, regardless of whether a module at
  // such path is known to the story.
  RunningModInfo* FindAnchor(RunningModInfo* running_mod_info);

  const std::string story_id_;
  fuchsia::modular::StoryState runtime_state_ = fuchsia::modular::StoryState::STOPPED;

  StoryProviderImpl* const story_provider_impl_;  // Not owned.
  SessionStorage* const session_storage_;         // Not owned.
  StoryStorage* const story_storage_;             // Not owned.

  inspect::Node* story_inspect_node_;  // Not owned.

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

  std::vector<std::unique_ptr<RunningModInfo>> running_mod_infos_;

  // This is the source of truth on which activities are currently ongoing in
  // the story's modules.
  fidl::BindingSet<fuchsia::modular::OngoingActivity, std::unique_ptr<OngoingActivityImpl>>
      ongoing_activities_;

  // Asynchronous operations are sequenced in a queue.
  OperationQueue operation_queue_;

  fxl::WeakPtrFactory<StoryControllerImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerImpl);
};

// NOTE: This is only exposed publicly for testing.
bool ShouldRestartModuleForNewIntent(const fuchsia::modular::Intent& old_intent,
                                     const fuchsia::modular::Intent& new_intent);

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
