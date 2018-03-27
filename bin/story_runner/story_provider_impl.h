// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
#define PERIDOT_BIN_STORY_RUNNER_STORY_PROVIDER_IMPL_H_

#include <map>
#include <memory>
#include <set>

#include <fuchsia/cpp/views_v1_token.h>
#include "lib/async/cpp/operation.h"
#include "lib/config/fidl/config.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "lib/module_resolver/fidl/module_resolver.fidl.h"
#include "lib/story/fidl/story_data.fidl.h"
#include "lib/story/fidl/story_provider.fidl.h"
#include "lib/story/fidl/story_shell.fidl.h"
#include "lib/user/fidl/focus.fidl.h"
#include "lib/user_intelligence/fidl/user_intelligence_provider.fidl.h"
#include "peridot/bin/agent_runner/agent_runner.h"
#include "peridot/bin/component/component_context_impl.h"
#include "peridot/bin/component/message_queue_manager.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/proxy.h"
#include "peridot/lib/fidl/scope.h"
#include "peridot/lib/ledger_client/ledger_client.h"
#include "peridot/lib/ledger_client/page_client.h"
#include "peridot/lib/ledger_client/types.h"

namespace modular {
class Resolver;
class StoryControllerImpl;

class StoryProviderImpl : StoryProvider, PageClient, FocusWatcher {
 public:
  StoryProviderImpl(
      Scope* user_scope,
      std::string device_id,
      LedgerClient* ledger_client,
      LedgerPageId page_id,
      AppConfigPtr story_shell,
      const ComponentContextInfo& component_context_info,
      FocusProviderPtr focus_provider,
      maxwell::UserIntelligenceProvider* user_intelligence_provider,
      ModuleResolver* module_resolver,
      bool test);

  ~StoryProviderImpl() override;

  void Connect(f1dl::InterfaceRequest<StoryProvider> request);

  void StopAllStories(const std::function<void()>& callback);

  // Stops serving the StoryProvider interface and stops all stories.
  void Teardown(const std::function<void()>& callback);

  // Called by StoryControllerImpl.
  const Scope* user_scope() const { return user_scope_; }

  // The device ID for this user/device.
  const std::string device_id() const { return device_id_; }

  // Called by StoryControllerImpl.
  const ComponentContextInfo& component_context_info() {
    return component_context_info_;
  }

  // Called by StoryControllerImpl.
  maxwell::UserIntelligenceProvider* user_intelligence_provider() {
    return user_intelligence_provider_;
  }

  // Called by StoryControllerImpl.
  ModuleResolver* module_resolver() { return module_resolver_; }

  // Called by StoryControllerImpl.
  const AppConfig& story_shell() const { return *story_shell_; }

  // Called by StoryControllerImpl.
  //
  // Returns an AppClient rather than taking an interface request
  // as an argument because the application is preloaded.
  std::unique_ptr<AppClient<Lifecycle>> StartStoryShell(
      fidl::InterfaceRequest<views_v1_token::ViewOwner> request);

  // Called by StoryControllerImpl.
  void SetStoryInfoExtra(const f1dl::StringPtr& story_id,
                         const f1dl::StringPtr& name,
                         const f1dl::StringPtr& value,
                         const std::function<void()>& done);

  // |StoryProvider|, also used by StoryControllerImpl.
  void GetStoryInfo(const f1dl::StringPtr& story_id,
                    const GetStoryInfoCallback& callback) override;

  // Called by StoryControllerImpl. Sends request to FocusProvider
  void RequestStoryFocus(const f1dl::StringPtr& story_id);

  // Called by StoryControllerImpl.
  void NotifyStoryStateChange(const f1dl::StringPtr& story_id,
                              StoryState story_state);

  void DumpState(const std::function<void(const std::string&)>& callback);

 private:
  // |StoryProvider|
  void CreateStory(const f1dl::StringPtr& module_url,
                   const CreateStoryCallback& callback) override;

  // |StoryProvider|
  void CreateStoryWithInfo(
      const f1dl::StringPtr& module_url,
      f1dl::VectorPtr<StoryInfoExtraEntryPtr> extra_info,
      const f1dl::StringPtr& root_json,
      const CreateStoryWithInfoCallback& callback) override;

  // |StoryProvider|
  void DeleteStory(const f1dl::StringPtr& story_id,
                   const DeleteStoryCallback& callback) override;

  // |StoryProvider|
  void GetController(const f1dl::StringPtr& story_id,
                     f1dl::InterfaceRequest<StoryController> request) override;

  // |StoryProvider|
  void PreviousStories(const PreviousStoriesCallback& callback) override;

  // |StoryProvider|
  void RunningStories(const RunningStoriesCallback& callback) override;

  // |StoryProvider|
  void Watch(f1dl::InterfaceHandle<StoryProviderWatcher> watcher) override;

  // |StoryProvider|
  void Duplicate(f1dl::InterfaceRequest<StoryProvider> request) override;

  // |StoryProvider|
  void GetLinkPeer(const f1dl::StringPtr& story_id,
                   f1dl::VectorPtr<f1dl::StringPtr> module_path,
                   const f1dl::StringPtr& link_name,
                   f1dl::InterfaceRequest<Link> request) override;

  // |PageClient|
  void OnPageChange(const std::string& key, const std::string& value) override;

  // |PageClient|
  void OnPageDelete(const std::string& key) override;

  // |FocusWatcher|
  void OnFocusChange(FocusInfoPtr info) override;

  // Called by ContextHandler.
  void OnContextChange();

  void NotifyImportanceWatchers();

  void NotifyStoryWatchers(const StoryInfo* story_info, StoryState story_state);

  void MaybeLoadStoryShell();

  void MaybeLoadStoryShellDelayed();

  Scope* const user_scope_;

  // Unique ID generated for this user/device combination.
  const std::string device_id_;

  // Story provider writes story records to the root page, and creates
  // new pages for stories.
  LedgerClient* const ledger_client_;

  // The bindings for this instance.
  f1dl::BindingSet<StoryProvider> bindings_;

  // Used to preload story shell before it is requested.
  AppConfigPtr story_shell_;
  struct StoryShellConnection {
    std::unique_ptr<AppClient<Lifecycle>> story_shell_app;
    views_v1_token::ViewOwnerPtr story_shell_view;
  };
  std::unique_ptr<StoryShellConnection> preloaded_story_shell_;

  // When running in a test, we don't preload story shells, because then the
  // preloaded next instance of the story doesn't pass its test points.
  const bool test_;

  // Holds the story shell view proxies for running story shells.
  ProxySet proxies_;

  f1dl::InterfacePtrSet<StoryProviderWatcher> watchers_;

  // The story controllers of the currently active stories, indexed by their
  // story IDs.
  //
  // Only user logout or delete story calls ever remove story controllers from
  // this collection, but controllers for stopped stories stay in it.
  //
  // Also keeps a cached version of the StoryInfo for every story, to send it to
  // newly registered story provider watchers, and to story provider watchers
  // when only the story state changes.
  struct StoryControllerImplContainer {
    std::unique_ptr<StoryControllerImpl> impl;
    StoryInfoPtr current_info;
  };
  std::map<std::string, StoryControllerImplContainer> story_controller_impls_;

  const ComponentContextInfo component_context_info_;

  maxwell::UserIntelligenceProvider* const
      user_intelligence_provider_;  // Not owned.

  ModuleResolver* const module_resolver_;  // Not owned.

  // When a story gets created, or when it gets focused on this device, we write
  // a record of the current context in the story page. So we need to watch the
  // context and the focus. This serves to compute relative importance of
  // stories in the timeline, as determined by the current context.
  FocusProviderPtr focus_provider_;
  f1dl::Binding<FocusWatcher> focus_watcher_binding_;

  // Machinery to support StoryProvider.GetLinkPeer().
  struct LinkPeer;
  std::vector<std::unique_ptr<LinkPeer>> link_peers_;

  // This is a container of all operations that are currently enqueued to run in
  // a FIFO manner. All operations exposed via |StoryProvider| interface are
  // queued here.
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
  class MutateStoryDataCall;
  class CreateStoryCall;
  class DeleteStoryCall;
  class GetControllerCall;
  class StopAllStoriesCall;
  class StopStoryShellCall;
  class GetImportanceCall;
  class GetLinkPeerCall;
  class DumpStateCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
