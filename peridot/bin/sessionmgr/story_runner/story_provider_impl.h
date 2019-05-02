// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_PROVIDER_IMPL_H_

#include <map>
#include <memory>
#include <set>

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/scenic/snapshot/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>

#include "peridot/bin/sessionmgr/agent_runner/agent_runner.h"
#include "peridot/bin/sessionmgr/component_context_impl.h"
#include "peridot/bin/sessionmgr/message_queue/message_queue_manager.h"
#include "peridot/bin/sessionmgr/story/model/noop_story_model_storage.h"
#include "peridot/bin/sessionmgr/story/model/story_model_owner.h"
#include "peridot/bin/sessionmgr/story/system.h"
#include "peridot/bin/sessionmgr/story_runner/story_entity_provider.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/environment.h"
#include "peridot/lib/fidl/proxy.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"
#include "peridot/lib/module_manifest/module_facet_reader.h"

namespace modular {

class PresentationProvider;
class Resolver;
class SessionStorage;
class StoryControllerImpl;
class StoryStorage;

class StoryProviderImpl : fuchsia::modular::StoryProvider,
                          fuchsia::modular::FocusWatcher {
 public:
  StoryProviderImpl(
      Environment* user_environment, std::string device_id,
      SessionStorage* session_storage,
      fuchsia::modular::AppConfig story_shell_config,
      fuchsia::modular::StoryShellFactoryPtr story_shell_factory,
      const ComponentContextInfo& component_context_info,
      fuchsia::modular::FocusProviderPtr focus_provider,
      fuchsia::modular::UserIntelligenceProvider* user_intelligence_provider,
      fuchsia::modular::ModuleResolver* module_resolver,
      EntityProviderRunner* entity_provider_runner,
      modular::ModuleFacetReader* module_facet_reader,
      PresentationProvider* presentation_provider,
      fuchsia::ui::viewsv1::ViewSnapshotPtr view_snapshot, bool test);

  ~StoryProviderImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request);

  // Used when the session shell is swapped.
  void StopAllStories(fit::function<void()> callback);

  // The session shell to send story views to. It is not a constructor argument
  // because it is updated when the session shell is swapped.
  void SetSessionShell(fuchsia::modular::SessionShellPtr session_shell);

  // Stops serving the fuchsia::modular::StoryProvider interface and stops all
  // stories.
  void Teardown(fit::function<void()> callback);

  // Called by StoryControllerImpl.
  const Environment* user_environment() const { return user_environment_; }

  // The device ID for this user/device.
  const std::string device_id() const { return device_id_; }

  // Called by StoryControllerImpl.
  const ComponentContextInfo& component_context_info() {
    return component_context_info_;
  }

  // Called by StoryControllerImpl.
  fuchsia::modular::UserIntelligenceProvider* user_intelligence_provider() {
    return user_intelligence_provider_;
  }

  // Called by StoryControllerImpl.
  fuchsia::modular::ModuleResolver* module_resolver() {
    return module_resolver_;
  }

  fuchsia::modular::EntityResolver* entity_resolver() {
    return entity_provider_runner_;
  }

  modular::ModuleFacetReader* module_facet_reader() {
    return module_facet_reader_;
  }

  // Called by StoryControllerImpl.
  const fuchsia::modular::AppConfig& story_shell_config() const {
    return story_shell_config_;
  }

  // Called by StoryControllerImpl.
  std::unique_ptr<AsyncHolderBase> StartStoryShell(
      fidl::StringPtr story_id, fuchsia::ui::views::ViewToken view_token,
      fidl::InterfaceRequest<fuchsia::modular::StoryShell> story_shell_request);

  // Called by StoryControllerImpl.
  //
  // Returns nullptr if the StoryInfo for |story_id| is not cached.
  fuchsia::modular::StoryInfoPtr GetCachedStoryInfo(std::string story_id);

  // |fuchsia::modular::StoryProvider|.
  void GetStoryInfo(std::string story_id,
                    GetStoryInfoCallback callback) override;

  // Called by StoryControllerImpl. Sends request to
  // fuchsia::modular::FocusProvider
  void RequestStoryFocus(fidl::StringPtr story_id);

  // Called by StoryControllerImpl. Sends, using AttachView(), a token for the
  // view of the story identified by |story_id| to the current session shell.
  void AttachView(fidl::StringPtr story_id,
                  fuchsia::ui::viewsv1token::ViewOwnerPtr view_owner);

  // Called by StoryControllerImpl. Notifies, using DetachView(), the current
  // session shell that the view of the story identified by |story_id| is about
  // to close.
  void DetachView(fidl::StringPtr story_id, fit::function<void()> done);

  // Called by StoryControllerImpl.
  void NotifyStoryActivityChange(
      fidl::StringPtr story_id,
      fidl::VectorPtr<fuchsia::modular::OngoingActivityType>
          ongoing_activities);

  // Called by StoryControllerImpl. Sends request to
  // fuchsia::modular::SessionShell through PresentationProvider.
  void GetPresentation(
      fidl::StringPtr story_id,
      fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request);
  void WatchVisualState(
      fidl::StringPtr story_id,
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher);

  // Called by StoryControllerImpl. Takes a snapshot of the story by the given
  // |story_id|. Callback is returned with the snapshot of the story, or an
  // empty buffer (size == 0) if the snapshot could not be taken.
  void TakeSnapshot(fidl::StringPtr story_id,
                    fit::function<void(fuchsia::mem::Buffer)> callback);

  // Called by StoryControllerImpl. Creates a new view with the given
  // |view_token| and connects the snapshot loader with the given
  // |loader_request|.
  void StartSnapshotLoader(
      fuchsia::ui::views::ViewToken view_token,
      fidl::InterfaceRequest<fuchsia::scenic::snapshot::Loader> loader_request);

  // Creates an entity with the specified |type| and |data| in the story with
  // |story_id|.
  //
  // |callback| will be called with a reference to the created entity. If the
  // creation failed the |entity_request| is dropped.
  void CreateEntity(
      const std::string& story_id, fidl::StringPtr type,
      fuchsia::mem::Buffer data,
      fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request,
      fit::function<void(std::string /* entity_reference */)> callback);

  // Creates an entity with the specified |type| and |data| in the story with
  // |story_id|.
  //
  // The story provider guarantees the uniqueness of the EntityProvider
  // associated with any given story.
  void ConnectToStoryEntityProvider(
      const std::string& story_id,
      fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
          entity_provider_request);

 private:
  // |fuchsia::modular::StoryProvider|
  void GetController(std::string story_id,
                     fidl::InterfaceRequest<fuchsia::modular::StoryController>
                         request) override;

  // |fuchsia::modular::StoryProvider|
  void GetStories(
      fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
      PreviousStoriesCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void PreviousStories(PreviousStoriesCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void Watch(fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher>
                 watcher) override;

  // |fuchsia::modular::StoryProvider|
  void WatchActivity(
      fidl::InterfaceHandle<fuchsia::modular::StoryActivityWatcher> watcher)
      override;

  // |fuchsia::modular::FocusWatcher|
  void OnFocusChange(fuchsia::modular::FocusInfoPtr info) override;

  // Called by *session_storage_.
  void OnStoryStorageDeleted(fidl::StringPtr story_id);
  void OnStoryStorageUpdated(fidl::StringPtr story_id,
                             fuchsia::modular::internal::StoryData story_data);

  // Called indirectly through observation of loaded StoryModels. Calls
  // NotifyStoryWatchers().
  void NotifyStoryStateChange(fidl::StringPtr story_id);

  void NotifyStoryWatchers(
      const fuchsia::modular::internal::StoryData* story_data,
      fuchsia::modular::StoryState story_state,
      fuchsia::modular::StoryVisibilityState story_visibility_state);

  void MaybeLoadStoryShell();

  void MaybeLoadStoryShellDelayed();

  Environment* const user_environment_;

  SessionStorage* session_storage_;  // Not owned.

  // The service from the session shell run by the sessionmgr. Owned here
  // because only used from here.
  fuchsia::modular::SessionShellPtr session_shell_;

  // Unique ID generated for this user/device combination.
  const std::string device_id_;

  // The bindings for this instance.
  fidl::BindingSet<fuchsia::modular::StoryProvider> bindings_;

  // Component URL and arguments used to launch story shells.
  fuchsia::modular::AppConfig story_shell_config_;

  // Used to preload story shell before it is requested.
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>>
      preloaded_story_shell_app_;

  // Used to manufacture new StoryShells if not launching a new component for
  // every requested StoryShell instance.
  fuchsia::modular::StoryShellFactoryPtr story_shell_factory_;

  // When running in a test, we don't want to enable preloading of story shells,
  // because then the preloaded next instance of the story doesn't pass its test
  // points.
  const bool enable_story_shell_preload_;

  fidl::InterfacePtrSet<fuchsia::modular::StoryProviderWatcher> watchers_;
  fidl::InterfacePtrSet<fuchsia::modular::StoryActivityWatcher>
      activity_watchers_;

  // The story controllers of the currently active stories, indexed by their
  // story IDs.
  //
  // Only user logout or delete story calls ever remove story controllers from
  // this collection, but controllers for stopped stories stay in it.
  //
  // Also keeps a cached version of the StoryData for every story so it does
  // not have to be loaded from disk when querying about this story.
  struct StoryRuntimeContainer {
    // The executor on which asynchronous tasks are scheduled for this story.
    //
    // TODO(thatguy): Migrate all operations under |controller_impl| to use
    // fit::promise and |executor|. MF-117
    // TODO(thatguy): Once fit::scope is complete, share one executor for the
    // whole process and take advantage of fit::scope to auto-cancel tasks when
    // |this| dies.
    std::unique_ptr<fit::executor> executor;

    // StoryRuntime itself contains a StoryModelOwner and manages systems with
    // asynchronous initialization and teardown operations.
    std::unique_ptr<StoryModelOwner> model_owner;

    // For ease of memory management, we store all runtime systems in
    // |systems|.
    std::vector<std::unique_ptr<System>> systems;

    // This allows us to observe changes to the StoryModel owned by |runtime|.
    std::unique_ptr<StoryObserver> model_observer;

    // NOTE: The following are transitioning to StoryModel and associated
    // classes above, as outlined in MF-85.
    std::unique_ptr<StoryControllerImpl> controller_impl;
    std::unique_ptr<StoryStorage> storage;
    std::unique_ptr<StoryEntityProvider> entity_provider;
    fuchsia::modular::internal::StoryDataPtr current_data;
  };
  std::map<std::string, StoryRuntimeContainer> story_runtime_containers_;

  const ComponentContextInfo component_context_info_;

  fuchsia::modular::UserIntelligenceProvider* const
      user_intelligence_provider_;                           // Not owned.
  fuchsia::modular::ModuleResolver* const module_resolver_;  // Not owned.
  EntityProviderRunner* const entity_provider_runner_;       // Not owned.
  modular::ModuleFacetReader* const module_facet_reader_;    // Not owned.
  PresentationProvider* const presentation_provider_;        // Not owned.

  // When a story gets created, or when it gets focused on this device, we write
  // a record of the current context in the story page. So we need to watch the
  // context and the focus. This serves to compute relative importance of
  // stories in the timeline, as determined by the current context.
  fuchsia::modular::FocusProviderPtr focus_provider_;
  fidl::Binding<fuchsia::modular::FocusWatcher> focus_watcher_binding_;

  // Service provided by scenic to take snapshots of stories.
  fuchsia::ui::viewsv1::ViewSnapshotPtr view_snapshot_;

  // Cached mapping of story ID's to the story view koids. Used as a token to
  // take snapshots of stories. This is a temporary hack because koids are
  // guessable. The intention is to have this hack until the framework is
  // provided with a more secure API from scenic to take snapshots, which is
  // TBD.
  std::map<fidl::StringPtr, zx_koid_t> view_endpoints_;

  // Snapshot loader component which is used to create new snapshot views. There
  // is only one instance of this component running per session.
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> snapshot_loader_app_;

  // This is a container of all operations that are currently enqueued to run in
  // a FIFO manner. All operations exposed via |fuchsia::modular::StoryProvider|
  // interface are queued here.
  //
  // The advantage of doing this is that if an operation consists of multiple
  // asynchronous calls then no state needs to be maintained for incomplete /
  // pending operations.
  //
  // TODO(mesch): If a story provider operation invokes a story operation that
  // causes the story updating its story info state, that update operation gets
  // scheduled on this queue again, after the current operation. It would be
  // better to be able to schedule such an operation on the story queue because
  // it's a per story operation even if it affects the per story key in the root
  // page, and then the update of story info is bounded by the outer operation.
  OperationQueue operation_queue_;

  fxl::WeakPtrFactory<StoryProviderImpl> weak_factory_;

  // Operations implemented here.
  class LoadStoryRuntimeCall;
  class StopStoryCall;
  class StopAllStoriesCall;
  class StopStoryShellCall;
  class GetStoryEntityProviderCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
