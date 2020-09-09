// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_PROVIDER_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <lib/sys/inspect/cpp/component.h>

#include <map>
#include <memory>
#include <set>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/sessionmgr/agent_runner/agent_runner.h"
#include "src/modular/bin/sessionmgr/component_context_impl.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"
#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/lib/async/cpp/operation.h"
#include "src/modular/lib/deprecated_service_provider/service_provider_impl.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/environment.h"
#include "src/modular/lib/fidl/proxy.h"

namespace modular {

// StoryControllerImpl has a circular dependency on StoryProviderImpl.
class StoryControllerImpl;

class StoryProviderImpl : fuchsia::modular::StoryProvider {
 public:
  StoryProviderImpl(Environment* session_environment, SessionStorage* session_storage,
                    fuchsia::modular::session::AppConfig story_shell_config,
                    fuchsia::modular::StoryShellFactoryPtr story_shell_factory,
                    ComponentContextInfo component_context_info,
                    AgentServicesFactory* agent_services_factory, inspect::Node* root_node);

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
  Environment* session_environment() const { return session_environment_; }

  // Called by StoryControllerImpl.
  const ComponentContextInfo& component_context_info() { return component_context_info_; }

  // Called by StoryControllerImpl.
  AgentServicesFactory* agent_services_factory() { return agent_services_factory_; }

  // Called by StoryControllerImpl.
  const fuchsia::modular::session::AppConfig& story_shell_config() const {
    return story_shell_config_;
  }

  // Called by SessionmgrImpl.
  //
  // Returns a StoryControllerImpl ptr for |story_id| or nullptr if that story
  // is not running. The returned pointer is safe to use for the stack frame of
  // the calling function.
  StoryControllerImpl* GetStoryControllerImpl(std::string story_id);

  // Called by StoryControllerImpl.
  std::unique_ptr<AsyncHolderBase> StartStoryShell(
      std::string story_id, fuchsia::ui::views::ViewToken view_token,
      fidl::InterfaceRequest<fuchsia::modular::StoryShell> story_shell_request);

  // Called by StoryControllerImpl.
  //
  // Returns nullptr if the StoryInfo for |story_id| is not cached.
  fuchsia::modular::StoryInfo2Ptr GetCachedStoryInfo(std::string story_id);

  // |fuchsia::modular::StoryProvider|.
  void GetStoryInfo(std::string story_id, GetStoryInfoCallback callback) override;

  // |fuchsia::modular::StoryProvider|.
  void GetStoryInfo2(std::string story_id, GetStoryInfo2Callback callback) override;

  // Called by StoryControllerImpl. Sends, using AttachView(), a token for the
  // view of the story identified by |story_id| to the current session shell.
  void AttachView(std::string story_id, fuchsia::ui::views::ViewHolderToken view_holder_token);

  // Called by StoryControllerImpl. Notifies, using DetachView(), the current
  // session shell that the view of the story identified by |story_id| is about
  // to close.
  void DetachView(std::string story_id, fit::function<void()> done);

  // Converts a StoryInfo2 to StoryInfo.
  static fuchsia::modular::StoryInfo StoryInfo2ToStoryInfo(
      const fuchsia::modular::StoryInfo2& story_info_2);

  // Called by StoryProviderImpl when the StoryState changes.
  void NotifyStoryStateChange(std::string story_id);

 private:
  // |fuchsia::modular::StoryProvider|
  void GetController(std::string story_id,
                     fidl::InterfaceRequest<fuchsia::modular::StoryController> request) override;

  // |fuchsia::modular::StoryProvider|
  void GetStories(fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
                  GetStoriesCallback callback) override;

  // |fuchsia::modular::StoryProvider|
  void GetStories2(fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
                   GetStories2Callback callback) override;

  // |fuchsia::modular::StoryProvider|
  void Watch(fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher) override;

  // Called by *session_storage_.
  void OnStoryStorageDeleted(std::string story_id);
  void OnStoryStorageUpdated(std::string story_id,
                             const fuchsia::modular::internal::StoryData& story_data);

  void NotifyStoryWatchers(const fuchsia::modular::internal::StoryData* story_data,
                           fuchsia::modular::StoryState story_state);

  void MaybeLoadStoryShell();

  Environment* const session_environment_;  // Not owned.
  SessionStorage* session_storage_;         // Not owned.

  // SessionShell service served by the session shell component.
  fuchsia::modular::SessionShellPtr session_shell_;

  // The bindings for this instance.
  fidl::BindingSet<fuchsia::modular::StoryProvider> bindings_;
  fidl::InterfacePtrSet<fuchsia::modular::StoryProviderWatcher> watchers_;

  // Component URL and arguments used to launch story shells.
  fuchsia::modular::session::AppConfig story_shell_config_;

  // Services that story shells can connect to from their environment.
  component::ServiceProviderImpl story_shell_services_;

  // Used to preload story shell before it is requested.
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> preloaded_story_shell_app_;

  // Used to manufacture new StoryShells if not launching a new component for
  // every requested StoryShell instance.
  fuchsia::modular::StoryShellFactoryPtr story_shell_factory_;

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

    std::unique_ptr<StoryControllerImpl> controller_impl;
    std::shared_ptr<StoryStorage> storage;
    fuchsia::modular::internal::StoryDataPtr current_data;

    std::unique_ptr<inspect::Node> story_node;
    std::map<const std::string, inspect::StringProperty> annotation_inspect_properties;

    void InitializeInspect(std::string story_id, inspect::Node* session_inspect_node);
    void ResetInspect();
  };
  std::map<std::string, StoryRuntimeContainer> story_runtime_containers_;

  const ComponentContextInfo component_context_info_;

  AgentServicesFactory* const agent_services_factory_;  // Not owned.

  inspect::Node* session_inspect_node_;

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

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryProviderImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_PROVIDER_IMPL_H_
