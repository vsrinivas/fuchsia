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
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/sessionmgr/agent_runner/agent_runner.h"
#include "peridot/bin/sessionmgr/component_context_impl.h"
#include "peridot/bin/sessionmgr/message_queue/message_queue_manager.h"
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
      SessionStorage* session_storage, fuchsia::modular::AppConfig story_shell,
      const ComponentContextInfo& component_context_info,
      fuchsia::modular::FocusProviderPtr focus_provider,
      fuchsia::modular::UserIntelligenceProvider* user_intelligence_provider,
      fuchsia::modular::ModuleResolver* module_resolver,
      fuchsia::modular::EntityResolver* entity_resolver,
      modular::ModuleFacetReader* module_facet_reader,
      PresentationProvider* presentation_provider,
      fuchsia::ui::viewsv1::ViewSnapshotPtr view_snapshot, bool test);

  ~StoryProviderImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request);

  void StopAllStories(const std::function<void()>& callback);

  // Stops serving the fuchsia::modular::StoryProvider interface and stops all
  // stories.
  void Teardown(const std::function<void()>& callback);

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
    return entity_resolver_;
  }

  modular::ModuleFacetReader* module_facet_reader() {
    return module_facet_reader_;
  }

  // Called by StoryControllerImpl.
  const fuchsia::modular::AppConfig& story_shell() const {
    return story_shell_;
  }

  // Called by StoryControllerImpl.
  //
  // Returns an AppClient rather than taking an interface request
  // as an argument because the application is preloaded.
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> StartStoryShell(
      fidl::StringPtr story_id,
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> request);

  // |fuchsia::modular::StoryProvider|, also used by StoryControllerImpl.
  void GetStoryInfo(fidl::StringPtr story_id,
                    GetStoryInfoCallback callback) override;

  // Called by StoryControllerImpl. Sends request to
  // fuchsia::modular::FocusProvider
  void RequestStoryFocus(fidl::StringPtr story_id);

  // Called by StoryControllerImpl.
  void NotifyStoryStateChange(
      fidl::StringPtr story_id, fuchsia::modular::StoryState story_state,
      fuchsia::modular::StoryVisibilityState story_visibility_state);

  // Called by StoryControllerImpl.
  void NotifyStoryActivityChange(
      fidl::StringPtr story_id,
      fidl::VectorPtr<fuchsia::modular::OngoingActivityType>
          ongoing_activities);

  // Called by StoryControllerImpl. Sends request to fuchsia::modular::SessionShell
  // through PresentationProvider.
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
  // |view_owner_request| and connects the snapshot loader with the given
  // |loader_request|.
  void StartSnapshotLoader(
      fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request,
      fidl::InterfaceRequest<fuchsia::scenic::snapshot::Loader> loader_request);

 private:
  // |fuchsia::modular::StoryProvider|
  void CreateStory(fidl::StringPtr module_url,
                   CreateStoryCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void CreateStoryWithOptions(fuchsia::modular::StoryOptions story_options,
                              CreateStoryWithOptionsCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void CreateStoryWithInfo(
      fidl::StringPtr module_url,
      fidl::VectorPtr<fuchsia::modular::StoryInfoExtraEntry> extra_info,
      fidl::StringPtr root_json, CreateStoryWithInfoCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void DeleteStory(fidl::StringPtr story_id,
                   DeleteStoryCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void GetController(fidl::StringPtr story_id,
                     fidl::InterfaceRequest<fuchsia::modular::StoryController>
                         request) override;

  // |fuchsia::modular::StoryProvider|
  void GetStories(
      fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
      PreviousStoriesCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void PreviousStories(PreviousStoriesCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void RunningStories(RunningStoriesCallback callback) override;

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

  void NotifyImportanceWatchers();

  void NotifyStoryWatchers(
      const fuchsia::modular::internal::StoryData* const story_data,
      fuchsia::modular::StoryState story_state,
      fuchsia::modular::StoryVisibilityState story_visibility_state);

  void MaybeLoadStoryShell();

  void MaybeLoadStoryShellDelayed();

  Environment* const user_environment_;

  SessionStorage* session_storage_;  // Not owned.

  // Unique ID generated for this user/device combination.
  const std::string device_id_;

  // The bindings for this instance.
  fidl::BindingSet<fuchsia::modular::StoryProvider> bindings_;

  // Used to preload story shell before it is requested.
  fuchsia::modular::AppConfig story_shell_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>>
      preloaded_story_shell_app_;

  // When running in a test, we don't preload story shells, because then the
  // preloaded next instance of the story doesn't pass its test points.
  const bool test_;

  fidl::InterfacePtrSet<fuchsia::modular::StoryProviderWatcher> watchers_;
  fidl::InterfacePtrSet<fuchsia::modular::StoryActivityWatcher>
      activity_watchers_;

  // The story controllers of the currently active stories, indexed by their
  // story IDs.
  //
  // Only user logout or delete story calls ever remove story controllers from
  // this collection, but controllers for stopped stories stay in it.
  //
  // Also keeps a cached version of the fuchsia::modular::StoryInfo for every
  // story, to send it to newly registered story provider watchers, and to story
  // provider watchers when only the story state changes.
  struct StoryControllerImplContainer {
    std::unique_ptr<StoryControllerImpl> impl;
    std::unique_ptr<StoryStorage> storage;
    fuchsia::modular::StoryInfoPtr current_info;
  };
  std::map<std::string, StoryControllerImplContainer> story_controller_impls_;

  const ComponentContextInfo component_context_info_;

  fuchsia::modular::UserIntelligenceProvider* const
      user_intelligence_provider_;                           // Not owned.
  fuchsia::modular::ModuleResolver* const module_resolver_;  // Not owned.
  fuchsia::modular::EntityResolver* const entity_resolver_;  // Not owned.
  modular::ModuleFacetReader* const module_facet_reader_;  // Not owned.
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
  class CreateStoryCall;
  class DeleteStoryCall;
  class GetControllerCall;
  class StopAllStoriesCall;
  class StopStoryShellCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
