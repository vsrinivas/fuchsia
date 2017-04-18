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
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/lib/fidl/page_client.h"
#include "apps/modular/lib/fidl/scope.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/modular/services/story/story_data.fidl.h"
#include "apps/modular/services/story/story_provider.fidl.h"
#include "apps/modular/src/agent_runner/agent_runner.h"
#include "apps/modular/src/component/component_context_impl.h"
#include "apps/modular/src/component/message_queue_manager.h"
#include "apps/modular/src/story_runner/story_storage_impl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"

namespace modular {
class Resolver;
class StoryImpl;

// The link name under which the user runner link is stored in the
// root page of the user.
constexpr char kUserShellKey[] = "user-shell-link";

class StoryProviderImpl : StoryProvider, ledger::PageWatcher {
 public:
  StoryProviderImpl(
      const Scope* user_scope,
      ledger::Ledger* ledger,
      ledger::Page* root_page,
      AppConfigPtr story_shell,
      const ComponentContextInfo& component_context_info,
      maxwell::UserIntelligenceProvider* user_intelligence_provider);

  ~StoryProviderImpl() override;

  void Connect(fidl::InterfaceRequest<StoryProvider> request);

  // Called by empty binding set handler of StoryImpl to remove the
  // corresponding entry.
  void PurgeController(const std::string& story_id);

  // Used by StoryImpl.
  const Scope* user_scope() const { return user_scope_; }
  const ComponentContextInfo& component_context_info() {
    return component_context_info_;
  }
  maxwell::UserIntelligenceProvider* user_intelligence_provider() {
    return user_intelligence_provider_;
  }
  const AppConfig& story_shell() const { return *story_shell_; }

  void SetStoryInfoExtra(const fidl::String& story_id,
                         const fidl::String& name,
                         const fidl::String& value,
                         const std::function<void()>& callback);

  void SetStoryState(const fidl::String& story_id,
                     bool running,
                     StoryState state);

  // |StoryProvider|, also used by StoryImpl.
  void GetStoryInfo(const fidl::String& story_id,
                    const GetStoryInfoCallback& callback) override;

 private:
  using FidlStringMap = fidl::Map<fidl::String, fidl::String>;

  // |StoryProvider|
  void CreateStory(const fidl::String& url,
                   const CreateStoryCallback& callback) override;

  // |StoryProvider|
  void CreateStoryWithInfo(
      const fidl::String& url,
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
  void Watch(fidl::InterfaceHandle<StoryProviderWatcher> watcher) override;

  // |StoryProvider|
  void Duplicate(fidl::InterfaceRequest<StoryProvider> request) override;

  // |PageWatcher|
  void OnChange(ledger::PageChangePtr page,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override;

  const Scope* const user_scope_;

  // Story provider writes story records to the root page, and creates
  // new pages for stories.
  ledger::Ledger* const ledger_;
  ledger::Page* const root_page_;

  // The bindings for this instance.
  fidl::BindingSet<StoryProvider> bindings_;

  // We can only accept binding requests once the instance is fully
  // initalized. So we queue them up initially.
  bool ready_{};
  std::vector<fidl::InterfaceRequest<StoryProvider>> requests_;

  AppConfigPtr story_shell_;

  // A list of IDs of *all* stories available on a user's ledger.
  std::unordered_set<std::string> story_ids_;

  // The last snapshot received from the root page.
  PageClient root_client_;

  fidl::Binding<ledger::PageWatcher> page_watcher_binding_;

  fidl::InterfacePtrSet<StoryProviderWatcher> watchers_;

  std::unordered_map<std::string, std::unique_ptr<StoryImpl>>
      story_controllers_;

  const ComponentContextInfo component_context_info_;

  maxwell::UserIntelligenceProvider* const
      user_intelligence_provider_;  // Not owned.

  // This is a container of all operations that are currently enqueued
  // to run in a FIFO manner. All operations exposed via
  // |StoryProvider| interface are queued here.
  //
  // The advantage of doing this is that if an operation consists of
  // multiple asynchronous calls then no state needs to be maintained
  // for incomplete / pending operations.
  //
  // TODO(mesch,alhaad): At some point we might want to increase
  // concurrency and have one operation queue per story for those
  // operations that only affect one story.
  //
  // TODO(mesch): No story provider operation can invoke a story
  // controller operation that would cause the story controller
  // updating its story info state, because that would be a nested
  // operation and it would deadlock. There are no such operations
  // right now, but we need to establish a pattern that makes it
  // simply and obvious how to not introduce deadlocks.
  OperationQueue operation_queue_;

  // Operations implemented here.
  class GetStoryDataCall;
  class WriteStoryDataCall;
  class SetStoryInfoExtraCall;
  class SetStoryStateCall;
  class CreateStoryCall;
  class DeleteStoryCall;
  class GetControllerCall;
  class PreviousStoriesCall;

  // Represents a delete operation that was started via DeleteStory()
  // and is awaiting the corresponding PageWatcher::OnChange() call
  // before the operation is Done(). Because operations run in a
  // queue, there can be at most one DeleteStoryCall operation pending
  // at a time at a time.
  //
  // Delete operations taking place on remote devices can still
  // trigger a new delete operation (but those are queued after each
  // other).
  std::pair<std::string, DeleteStoryCall*> pending_deletion_;

  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
