// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_STORY_PROVIDER_IMPL_H_

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/maxwell/services/user/user_intelligence_provider.fidl.h"
#include "peridot/lib/fidl/operation.h"
#include "peridot/lib/fidl/proxy.h"
#include "peridot/lib/fidl/scope.h"
#include "peridot/lib/ledger/ledger_client.h"
#include "peridot/lib/ledger/page_client.h"
#include "peridot/lib/ledger/types.h"
#include "lib/config/fidl/config.fidl.h"
#include "lib/story/fidl/story_data.fidl.h"
#include "lib/story/fidl/story_provider.fidl.h"
#include "lib/story/fidl/story_shell.fidl.h"
#include "lib/user/fidl/focus.fidl.h"
#include "peridot/bin/agent_runner/agent_runner.h"
#include "peridot/bin/component/component_context_impl.h"
#include "peridot/bin/component/message_queue_manager.h"
#include "peridot/bin/story_runner/context_handler.h"
#include "peridot/bin/story_runner/story_storage_impl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/macros.h"

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
      maxwell::IntelligenceServices* intelligence_services,
      maxwell::UserIntelligenceProvider* user_intelligence_provider);

  ~StoryProviderImpl() override;

  void Connect(fidl::InterfaceRequest<StoryProvider> request);

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
  const AppConfig& story_shell() const { return *story_shell_; }

  // Called by StoryControllerImpl.
  //
  // Returns an ApplicationControllerPtr rather than taking an interface request
  // as an argument because the application is preloaded.
  app::ApplicationControllerPtr StartStoryShell(
      fidl::InterfaceHandle<StoryContext> story_context,
      fidl::InterfaceRequest<StoryShell> story_shell_request,
      fidl::InterfaceRequest<mozart::ViewOwner> view_request);

  // Called by StoryControllerImpl.
  void SetStoryInfoExtra(const fidl::String& story_id,
                         const fidl::String& name,
                         const fidl::String& value,
                         const std::function<void()>& done);

  // |StoryProvider|, also used by StoryControllerImpl.
  void GetStoryInfo(const fidl::String& story_id,
                    const GetStoryInfoCallback& callback) override;

  // Called by StoryControllerImpl. Sends request to FocusProvider
  void RequestStoryFocus(const fidl::String& story_id);

  // Called by StoryControllerImpl.
  void NotifyStoryStateChange(const fidl::String& story_id,
                              StoryState story_state);

 private:
  using FidlStringMap = fidl::Map<fidl::String, fidl::String>;
  using ImportanceMap = fidl::Map<fidl::String, float>;

  // |StoryProvider|
  void CreateStory(const fidl::String& module_url,
                   const CreateStoryCallback& callback) override;

  // |StoryProvider|
  void CreateStoryWithInfo(
      const fidl::String& module_url,
      FidlStringMap extra_info,
      const fidl::String& root_json,
      const CreateStoryWithInfoCallback& callback) override;

  // |StoryProvider|
  void DeleteStory(const fidl::String& story_id,
                   const DeleteStoryCallback& callback) override;

  // |StoryProvider|
  void GetController(const fidl::String& story_id,
                     fidl::InterfaceRequest<StoryController> request) override;

  // |StoryProvider|
  void PreviousStories(const PreviousStoriesCallback& callback) override;

  // |StoryProvider|
  void RunningStories(const RunningStoriesCallback& callback) override;

  // |StoryProvider|
  void Watch(fidl::InterfaceHandle<StoryProviderWatcher> watcher) override;

  // |StoryProvider|
  void GetImportance(const GetImportanceCallback& callback) override;

  // |StoryProvider|
  void WatchImportance(
      fidl::InterfaceHandle<StoryImportanceWatcher> watcher) override;

  // |StoryProvider|
  void Duplicate(fidl::InterfaceRequest<StoryProvider> request) override;

  // |StoryProvider|
  void GetLinkPeer(const fidl::String& story_id,
                   fidl::Array<fidl::String> module_path,
                   const fidl::String& link_name,
                   fidl::InterfaceRequest<Link> request) override;

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

  StoryContextLogPtr MakeLogEntry(StorySignal signal);

  void LoadStoryShell();

  Scope* const user_scope_;

  // Unique ID generated for this user/device combination.
  const std::string device_id_;

  // Story provider writes story records to the root page, and creates
  // new pages for stories.
  LedgerClient* const ledger_client_;

  // The bindings for this instance.
  fidl::BindingSet<StoryProvider> bindings_;

  // Used to preload story shell before it is requested.
  AppConfigPtr story_shell_;
  struct StoryShellConnection {
    app::ApplicationControllerPtr story_shell_controller;
    app::ServiceProviderPtr story_shell_services;
    mozart::ViewOwnerPtr story_shell_view;
  };
  std::unique_ptr<StoryShellConnection> preloaded_story_shell_;

  // Holds the story shell view proxies for running story shells.
  ProxySet proxies_;

  fidl::InterfacePtrSet<StoryProviderWatcher> watchers_;

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
  std::unordered_map<std::string, StoryControllerImplContainer>
      story_controller_impls_;

  const ComponentContextInfo component_context_info_;

  maxwell::UserIntelligenceProvider* const
      user_intelligence_provider_;  // Not owned.

  // When a story gets created, or when it gets focused on this device, we write
  // a record of the current context in the story page. So we need to watch the
  // context and the focus. This serves to compute relative importance of
  // stories in the timeline, as determined by the current context.
  ContextHandler context_handler_;
  FocusProviderPtr focus_provider_;
  fidl::Binding<FocusWatcher> focus_watcher_binding_;
  fidl::InterfacePtrSet<StoryImportanceWatcher> importance_watchers_;

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

  // Operations implemented here.
  class MutateStoryDataCall;
  class CreateStoryCall;
  class DeleteStoryCall;
  class GetControllerCall;
  class TeardownCall;
  class GetImportanceCall;
  class GetLinkPeerCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
