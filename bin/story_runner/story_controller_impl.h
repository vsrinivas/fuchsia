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

#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/async/cpp/operation.h"
#include "lib/component/fidl/component_context.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/struct_ptr.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/module/fidl/module_controller.fidl.h"
#include "lib/module/fidl/module_data.fidl.h"
#include "lib/story/fidl/per_device_story_info.fidl.h"
#include "lib/story/fidl/story_controller.fidl.h"
#include "lib/story/fidl/story_data.fidl.h"
#include "lib/story/fidl/story_shell.fidl.h"
#include "lib/surface/fidl/surface.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/lib/fidl/context.h"
#include "peridot/lib/fidl/scope.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {

class LinkImpl;
class ModuleControllerImpl;
class ModuleContextImpl;
class StoryProviderImpl;

constexpr char kRootLink[] = "root";
constexpr char kRootModuleName[] = "root";

// HACK(mesch): The context topics that influence story importance is hardcoded
// to a single one right now. This will be generalized, but we cannot simply
// look at the whole context, because it's too big.
constexpr char kStoryImportanceContext[] = "location/home_work";

// The story runner, which holds all the links and runs all the modules as well
// as the story shell. It also implements the StoryController service to give
// clients control over the story.
class StoryControllerImpl : PageClient, StoryController, StoryContext {
 public:
  StoryControllerImpl(const fidl::String& story_id,
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
  void AddForCreate(const fidl::String& module_name,
                    const fidl::String& module_url,
                    const fidl::String& link_name,
                    const fidl::String& link_json,
                    const std::function<void()>& done);

  // Called by StoryProviderImpl.
  StoryState GetStoryState() const;
  void Log(StoryContextLogPtr log_entry);
  void Sync(const std::function<void()>& done);
  void GetImportance(const ContextState& context_state,
                     const std::function<void(float)>& result);

  // Called by ModuleControllerImpl and ModuleContextImpl.
  void FocusModule(const fidl::Array<fidl::String>& module_path);

  // Called by ModuleControllerImpl.
  void DefocusModule(const fidl::Array<fidl::String>& module_path);

  // Called by ModuleControllerImpl and ModuleWatcherImpl.
  void StopModule(const fidl::Array<fidl::String>& module_path,
                  const std::function<void()>& done);

  // Called by ModuleControllerImpl.
  //
  // Releases ownership of |controller|, which deletes itself after return.
  void ReleaseModule(ModuleControllerImpl* module_controller_impl);

  // Called by ModuleContextImpl.
  const fidl::String& GetStoryId() const;

  // Called by ModuleContextImpl.
  void RequestStoryFocus();

  // Called by ModuleContextImpl.
  void ConnectLinkPath(LinkPathPtr link_path,
                       fidl::InterfaceRequest<Link> request);

  // Called by ModuleContextImpl.
  void StartModule(
      const fidl::Array<fidl::String>& parent_module_path,
      const fidl::String& module_name,
      const fidl::String& module_url,
      const fidl::String& link_name,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller_request,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      ModuleSource module_source);

  // Called by ModuleContextImpl and AddModule.
  void StartModuleInShell(
      const fidl::Array<fidl::String>& parent_module_path,
      const fidl::String& module_name,
      const fidl::String& module_url,
      const fidl::String& link_name,
      fidl::InterfaceHandle<app::ServiceProvider> outgoing_services,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<ModuleController> module_controller_request,
      SurfaceRelationPtr surface_relation,
      bool focus,
      ModuleSource module_source);

 private:
  class ModuleWatcherImpl;

  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // |StoryController|
  void GetInfo(const GetInfoCallback& callback) override;
  void SetInfoExtra(const fidl::String& name,
                    const fidl::String& value,
                    const SetInfoExtraCallback& callback) override;
  void Start(fidl::InterfaceRequest<mozart::ViewOwner> request) override;
  void Stop(const StopCallback& done) override;
  void Watch(fidl::InterfaceHandle<StoryWatcher> watcher) override;
  void AddModule(fidl::Array<fidl::String> module_path,
                 const fidl::String& module_name,
                 const fidl::String& module_url,
                 const fidl::String& link_name,
                 SurfaceRelationPtr surface_relation) override;
  void GetActiveModules(fidl::InterfaceHandle<StoryModulesWatcher> watcher,
                        const GetActiveModulesCallback& callback) override;
  void GetModules(const GetModulesCallback& callback) override;
  void GetModuleController(
      fidl::Array<fidl::String> module_path,
      fidl::InterfaceRequest<ModuleController> request) override;
  void GetActiveLinks(fidl::InterfaceHandle<StoryLinksWatcher> watcher,
                      const GetActiveLinksCallback& callback) override;
  void GetLink(fidl::Array<fidl::String> module_path,
               const fidl::String& name,
               fidl::InterfaceRequest<Link> request) override;

  // Phases of Start() broken out into separate methods.
  void StartStoryShell(fidl::InterfaceRequest<mozart::ViewOwner> request);

  // Misc internal helpers.
  void NotifyStateChange();
  void DisposeLink(LinkImpl* link);
  void AddModuleWatcher(ModuleControllerPtr module_controller,
                        const fidl::Array<fidl::String>& module_path);
  void OnRootStateChange(ModuleState state);
  void ProcessPendingViews();

  // The ID of the story, its state and the context to obtain it from and
  // persist it to.
  const fidl::String story_id_;

  // This is the canonical source for state. The value in the ledger is just a
  // write-behind copy of this value.
  StoryState state_{StoryState::INITIAL};

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
  app::ApplicationControllerPtr story_shell_controller_;
  StoryShellPtr story_shell_;
  fidl::Binding<StoryContext> story_context_binding_;

  // Needed to hold on to a running story. They get reset on Stop().
  struct ExternalModule {
    std::unique_ptr<ModuleWatcherImpl> module_watcher_impl;
    ModuleControllerPtr module_controller;
  };
  std::vector<ExternalModule> external_modules_;

  // The module instances (identified by their serialized module paths) already
  // known to story shell. Does not include modules whose views are pending and
  // not yet sent to story shell.
  std::set<std::string> connected_views_;

  // Holds a running module (that is displayed by story shell) 's view until its
  // parent is connected to story shell.
  struct ModuleView {
    std::string parent_view_id;
    mozart::ViewOwnerPtr view_owner;
    SurfaceRelationPtr surface_relation;
  };
  std::map<std::string, ModuleView> pending_views_;

  // The first ingredient of a story: Modules. For each Module in the Story,
  // there is one Connection to it.
  struct Connection {
    std::unique_ptr<ModuleContextImpl> module_context_impl;
    std::unique_ptr<ModuleControllerImpl> module_controller_impl;
  };
  std::vector<Connection> connections_;

  // The second ingredient of a story: Links. They connect Modules.
  std::vector<std::unique_ptr<LinkImpl>> links_;

  // A dummy service that allows applications that can run both as modules in a
  // story and standalone from the shell to determine whether they are in a
  // story. See story_marker.fidl for more details.
  class StoryMarkerImpl;
  std::unique_ptr<StoryMarkerImpl> story_marker_impl_;

  // A collection of services, scoped to this Story, for use by intelligent
  // Modules.
  maxwell::IntelligenceServicesPtr intelligence_services_;

  // Asynchronous operations are sequenced in a queue.
  OperationQueue operation_queue_;

  // Operations implemented here.
  class StartModuleCall;
  class StartModuleInShellCall;
  class AddModuleCall;
  class AddForCreateCall;
  class StopCall;
  class StopModuleCall;
  class DeleteCall;
  class StartCall;
  class GetImportanceCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryControllerImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_STORY_CONTROLLER_IMPL_H_
