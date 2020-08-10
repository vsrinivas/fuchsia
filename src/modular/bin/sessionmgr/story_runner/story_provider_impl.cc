// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story_runner/story_provider_impl.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>
#include <utility>
#include <vector>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/uuid/uuid.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/bin/sessionmgr/annotations.h"
#include "src/modular/bin/sessionmgr/presentation_provider.h"
#include "src/modular/bin/sessionmgr/storage/session_storage.h"
#include "src/modular/bin/sessionmgr/storage/story_storage.h"
#include "src/modular/bin/sessionmgr/story_runner/story_controller_impl.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/array_to_string.h"
#include "src/modular/lib/fidl/clone.h"
#include "src/modular/lib/fidl/proxy.h"

namespace modular {

class StoryProviderImpl::StopStoryCall : public Operation<> {
 public:
  using StoryRuntimesMap = std::map<std::string, struct StoryRuntimeContainer>;

  StopStoryCall(std::string story_id, const bool skip_notifying_sessionshell,
                StoryRuntimesMap* const story_runtime_containers, ResultCall result_call)
      : Operation("StoryProviderImpl::StopStoryCall", std::move(result_call)),
        story_id_(std::move(story_id)),
        skip_notifying_sessionshell_(skip_notifying_sessionshell),
        story_runtime_containers_(story_runtime_containers) {}

 private:
  void Run() override {
    FlowToken flow{this};

    auto i = story_runtime_containers_->find(story_id_);
    if (i == story_runtime_containers_->end()) {
      FX_LOGS(WARNING) << "I was told to stop story " << story_id_ << ", but I can't find it.";
      return;
    }

    FX_DCHECK(i->second.controller_impl != nullptr);
    i->second.controller_impl->Teardown(skip_notifying_sessionshell_,
                                        [weak_ptr = GetWeakPtr(), this, flow] {
                                          // Ensure |story_runtime_containers_| has not been
                                          // destroyed.
                                          //
                                          // This operation and its parent |StoryProviderImpl| may
                                          // be destroyed before this callback executes, for example
                                          // when |StoryProviderImpl.Teardown| times out. When this
                                          // happens, `operation_queue_` and this operation are
                                          // destroyed before |story_runtime_containers_|,
                                          // invalidating |weak_ptr|.
                                          if (!weak_ptr)
                                            return;
                                          story_runtime_containers_->erase(story_id_);
                                        });
  }

 private:
  const std::string story_id_;
  const bool skip_notifying_sessionshell_;
  StoryRuntimesMap* const story_runtime_containers_;
};

// Loads a StoryRuntimeContainer object and stores it in
// |story_provider_impl.story_runtime_containers_| so that the story is ready
// to be run.
class StoryProviderImpl::LoadStoryRuntimeCall : public Operation<StoryRuntimeContainer*> {
 public:
  LoadStoryRuntimeCall(StoryProviderImpl* const story_provider_impl,
                       SessionStorage* const session_storage, std::string story_id,
                       inspect::Node* root_node, ResultCall result_call)
      : Operation("StoryProviderImpl::LoadStoryRuntimeCall", std::move(result_call)),
        story_provider_impl_(story_provider_impl),
        session_storage_(session_storage),
        story_id_(std::move(story_id)),
        session_inspect_node_(root_node) {}

 private:
  void Run() override {
    FlowToken flow{this, &story_runtime_container_};

    // Use the existing controller, if possible.
    // This won't race against itself because it's managed by an operation
    // queue.
    auto i = story_provider_impl_->story_runtime_containers_.find(story_id_);
    if (i != story_provider_impl_->story_runtime_containers_.end()) {
      story_runtime_container_ = &i->second;
      return;
    }

    auto story_data = session_storage_->GetStoryData(story_id_);
    if (!story_data) {
      return;
      // Operation finishes since |flow| goes out of scope.
    }
    Cont(std::move(story_data), flow);
  }

  void Cont(fuchsia::modular::internal::StoryDataPtr story_data, FlowToken flow) {
    auto story_storage = session_storage_->GetStoryStorage(story_id_);
    struct StoryRuntimeContainer container {
      .executor = std::make_unique<async::Executor>(async_get_default_dispatcher()),
      .storage = std::move(story_storage), .current_data = std::move(story_data),
    };

    container.InitializeInspect(story_id_, session_inspect_node_);

    container.controller_impl =
        std::make_unique<StoryControllerImpl>(story_id_, session_storage_, container.storage.get(),
                                              story_provider_impl_, container.story_node.get());
    auto it =
        story_provider_impl_->story_runtime_containers_.emplace(story_id_, std::move(container));
    story_runtime_container_ = &it.first->second;
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned
  SessionStorage* const session_storage_;         // not owned
  const std::string story_id_;

  inspect::Node* session_inspect_node_;

  // Return value.
  StoryRuntimeContainer* story_runtime_container_ = nullptr;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;
};

class StoryProviderImpl::StopAllStoriesCall : public Operation<> {
 public:
  StopAllStoriesCall(StoryProviderImpl* const story_provider_impl, ResultCall result_call)
      : Operation("StoryProviderImpl::StopAllStoriesCall", std::move(result_call)),
        story_provider_impl_(story_provider_impl) {}

 private:
  void Run() override {
    FlowToken flow{this};

    for (auto& it : story_provider_impl_->story_runtime_containers_) {
      // Each callback has a copy of |flow| which only goes out-of-scope
      // once the story corresponding to |it| stops.
      //
      // TODO(thatguy): If the StoryControllerImpl is deleted before it can
      // complete StopWithoutNotifying(), we will never be called back and the
      // OperationQueue on which we're running will block.  Moving over to
      // fit::promise will allow us to observe cancellation.
      operations_.Add(std::make_unique<StopStoryCall>(
          it.first, true /* skip_notifying_sessionshell */,
          &story_provider_impl_->story_runtime_containers_, [flow] {}));
    }
  }

  OperationCollection operations_;

  StoryProviderImpl* const story_provider_impl_;  // not owned
};

class StoryProviderImpl::StopStoryShellCall : public Operation<> {
 public:
  StopStoryShellCall(StoryProviderImpl* const story_provider_impl, ResultCall result_call)
      : Operation("StoryProviderImpl::StopStoryShellCall", std::move(result_call)),
        story_provider_impl_(story_provider_impl) {}

 private:
  void Run() override {
    FlowToken flow{this};
    if (story_provider_impl_->preloaded_story_shell_app_) {
      // Calling Teardown() below will branch |flow| into normal and timeout
      // paths. |flow| must go out of scope when either of the paths
      // finishes.
      FlowTokenHolder branch{flow};
      story_provider_impl_->preloaded_story_shell_app_->Teardown(
          kBasicTimeout, [branch] { std::unique_ptr<FlowToken> flow = branch.Continue(); });
    }
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned
};

StoryProviderImpl::StoryProviderImpl(Environment* const session_environment,
                                     SessionStorage* const session_storage,
                                     fuchsia::modular::session::AppConfig story_shell_config,
                                     fuchsia::modular::StoryShellFactoryPtr story_shell_factory,
                                     const ComponentContextInfo& component_context_info,
                                     AgentServicesFactory* const agent_services_factory,
                                     PresentationProvider* const presentation_provider,
                                     inspect::Node* root_node)
    : session_environment_(session_environment),
      session_storage_(session_storage),
      story_shell_config_(std::move(story_shell_config)),
      story_shell_factory_(std::move(story_shell_factory)),
      component_context_info_(component_context_info),
      agent_services_factory_(agent_services_factory),
      presentation_provider_(presentation_provider),
      session_inspect_node_(root_node),
      weak_factory_(this) {
  session_storage_->set_on_story_deleted(
      [weak_ptr = weak_factory_.GetWeakPtr()](std::string story_id) {
        if (!weak_ptr)
          return;
        weak_ptr->OnStoryStorageDeleted(std::move(story_id));
      });
  session_storage_->set_on_story_updated(
      [weak_ptr = weak_factory_.GetWeakPtr()](std::string story_id,
                                              fuchsia::modular::internal::StoryData story_data) {
        if (!weak_ptr)
          return;
        weak_ptr->OnStoryStorageUpdated(std::move(story_id), std::move(story_data));
      });
}

StoryProviderImpl::~StoryProviderImpl() = default;

void StoryProviderImpl::Connect(fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void StoryProviderImpl::StopAllStories(fit::function<void()> callback) {
  operation_queue_.Add(std::make_unique<StopAllStoriesCall>(this, std::move(callback)));
}

void StoryProviderImpl::SetSessionShell(fuchsia::modular::SessionShellPtr session_shell) {
  // Not on operation queue, because it's called only after all stories have
  // been stopped or none are running yet, i.e. when no Operations that would
  // call this interface are scheduled. If there is an operation pending here,
  // then it would pertain to a story running in the new session shell started
  // by puppet master or an agent, so we must assign this now.
  //
  // TODO(mesch): It may well be that we need to revisit this when we support
  // starting stories, or swapping session shells, through puppet master, i.e.
  // from outside the session shell.
  //
  // TODO(mesch): Add a WARNING log if the operation is not empty.
  session_shell_ = std::move(session_shell);

  session_shell_.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "SessionShell service channel (from session shell component) "
                            << "unexpectedly closed.";
  });
}

void StoryProviderImpl::Teardown(fit::function<void()> callback) {
  // Closing all binding to this instance ensures that no new messages come
  // in, though previous messages need to be processed. The stopping of
  // stories is done on |operation_queue_| since that must strictly happen
  // after all pending messgages have been processed.
  bindings_.CloseAll();
  session_shell_.set_error_handler(nullptr);
  operation_queue_.Add(std::make_unique<StopAllStoriesCall>(this, [] {}));
  operation_queue_.Add(std::make_unique<StopStoryShellCall>(this, std::move(callback)));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::Watch(
    fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher) {
  auto watcher_ptr = watcher.Bind();
  for (const auto& item : story_runtime_containers_) {
    const auto& container = item.second;
    FX_CHECK(container.current_data->has_story_info());
    watcher_ptr->OnChange(StoryInfo2ToStoryInfo(container.current_data->story_info()),
                          container.controller_impl->runtime_state(),
                          fuchsia::modular::StoryVisibilityState::DEFAULT);
    watcher_ptr->OnChange2(CloneStruct(container.current_data->story_info()),
                           container.controller_impl->runtime_state(),
                           fuchsia::modular::StoryVisibilityState::DEFAULT);
  }
  watchers_.AddInterfacePtr(std::move(watcher_ptr));
}

StoryControllerImpl* StoryProviderImpl::GetStoryControllerImpl(std::string story_id) {
  auto it = story_runtime_containers_.find(story_id);
  if (it == story_runtime_containers_.end()) {
    return nullptr;
  }
  auto& container = it->second;
  return container.controller_impl.get();
}

std::unique_ptr<AsyncHolderBase> StoryProviderImpl::StartStoryShell(
    std::string story_id, fuchsia::ui::views::ViewToken view_token,
    fidl::InterfaceRequest<fuchsia::modular::StoryShell> story_shell_request) {
  // When we're supplied a StoryShellFactory, use it to get StoryShells instead
  // of launching the story shell as a separate component. In this case, there
  // is also nothing to preload, so ignore |preloaded_story_shell_app_|.
  if (story_shell_factory_) {
    story_shell_factory_->AttachStory(story_id, std::move(story_shell_request));

    auto on_teardown = [this, story_id = std::move(story_id)](fit::function<void()> done) {
      story_shell_factory_->DetachStory(story_id, std::move(done));
    };

    return std::make_unique<ClosureAsyncHolder>(/*name=*/story_id, std::move(on_teardown));
  }

  MaybeLoadStoryShell();

  fuchsia::ui::app::ViewProviderPtr view_provider;
  preloaded_story_shell_app_->services().ConnectToService(view_provider.NewRequest());
  view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);

  preloaded_story_shell_app_->services().ConnectToService(std::move(story_shell_request));

  auto story_shell_holder = std::move(preloaded_story_shell_app_);

  return story_shell_holder;
}

void StoryProviderImpl::MaybeLoadStoryShell() {
  if (preloaded_story_shell_app_) {
    return;
  }

  auto service_list = fuchsia::sys::ServiceList::New();
  for (auto service_name : component_context_info_.agent_runner->GetAgentServices()) {
    service_list->names.push_back(service_name);
  }
  component_context_info_.agent_runner->PublishAgentServices(story_shell_config_.url(),
                                                             &story_shell_services_);

  fuchsia::sys::ServiceProviderPtr service_provider;
  story_shell_services_.AddBinding(service_provider.NewRequest());
  service_list->provider = std::move(service_provider);

  preloaded_story_shell_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      session_environment_->GetLauncher(), CloneStruct(story_shell_config_), /*data_origin=*/"",
      std::move(service_list));
}

fuchsia::modular::StoryInfo2Ptr StoryProviderImpl::GetCachedStoryInfo(std::string story_id) {
  auto it = story_runtime_containers_.find(story_id);
  if (it == story_runtime_containers_.end()) {
    return nullptr;
  }
  FX_CHECK(it->second.current_data->has_story_info());
  return CloneOptional(it->second.current_data->story_info());
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStoryInfo(std::string story_id, GetStoryInfoCallback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>([this, story_id, callback = std::move(callback)] {
    auto story_data = session_storage_->GetStoryData(story_id);
    if (!story_data || !story_data->has_story_info()) {
      callback(nullptr);
      return;
    }
    callback(fidl::MakeOptional(StoryInfo2ToStoryInfo(story_data->story_info())));
  }));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStoryInfo2(std::string story_id, GetStoryInfo2Callback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>([this, story_id, callback = std::move(callback)] {
    auto story_data = session_storage_->GetStoryData(story_id);
    if (!story_data || !story_data->has_story_info()) {
      callback(fuchsia::modular::StoryInfo2{});
      return;
    }
    callback(std::move(*story_data->mutable_story_info()));
  }));
}

void StoryProviderImpl::AttachView(std::string story_id,
                                   fuchsia::ui::views::ViewHolderToken view_holder_token) {
  FX_CHECK(session_shell_) << "The session shell component must export and keep alive a "
                           << "fuchsia.modular.SessionShell service for sessionmgr to function.";
  fuchsia::modular::ViewIdentifier view_id;
  view_id.story_id = story_id;
  session_shell_->AttachView2(std::move(view_id), std::move(view_holder_token));
}

void StoryProviderImpl::DetachView(std::string story_id, fit::function<void()> done) {
  FX_CHECK(session_shell_) << "The session shell component must export and keep alive a "
                           << "fuchsia.modular.SessionShell service for sessionmgr to function.";
  fuchsia::modular::ViewIdentifier view_id;
  view_id.story_id = story_id;
  session_shell_->DetachView(std::move(view_id), std::move(done));
}

void StoryProviderImpl::NotifyStoryStateChange(std::string story_id) {
  auto it = story_runtime_containers_.find(story_id);
  if (it == story_runtime_containers_.end()) {
    // If this call arrives while DeleteStory() is in
    // progress, the story controller might already be gone
    // from here.
    return;
  }
  NotifyStoryWatchers(it->second.current_data.get(), it->second.controller_impl->runtime_state());
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetController(
    std::string story_id, fidl::InterfaceRequest<fuchsia::modular::StoryController> request) {
  operation_queue_.Add(std::make_unique<LoadStoryRuntimeCall>(
      this, session_storage_, story_id, session_inspect_node_,
      [request = std::move(request)](StoryRuntimeContainer* story_controller_container) mutable {
        if (story_controller_container) {
          story_controller_container->controller_impl->Connect(std::move(request));
        }
      }));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStories(
    fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
    GetStoriesCallback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>(
      [this, watcher = std::move(watcher), callback = std::move(callback)]() mutable {
        auto all_story_data = session_storage_->GetAllStoryData();
        std::vector<fuchsia::modular::StoryInfo> result;

        for (auto& story_data : all_story_data) {
          if (!story_data.has_story_info()) {
            continue;
          }
          result.push_back(StoryInfo2ToStoryInfo(story_data.story_info()));
        }

        if (watcher) {
          watchers_.AddInterfacePtr(watcher.Bind());
        }
        callback(std::move(result));
      }));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStories2(
    fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
    GetStories2Callback callback) {
  operation_queue_.Add(std::make_unique<SyncCall>(
      [this, watcher = std::move(watcher), callback = std::move(callback)]() mutable {
        auto all_story_data = session_storage_->GetAllStoryData();
        std::vector<fuchsia::modular::StoryInfo2> result;

        for (auto& story_data : all_story_data) {
          if (!story_data.has_story_info()) {
            continue;
          }
          result.push_back(std::move(*story_data.mutable_story_info()));
        }

        if (watcher) {
          watchers_.AddInterfacePtr(watcher.Bind());
        }
        callback(std::move(result));
      }));
}

void StoryProviderImpl::OnStoryStorageUpdated(std::string story_id,
                                              fuchsia::modular::internal::StoryData story_data) {
  // If we have a StoryRuntimeContainer for this story id, update our cached
  // StoryData and get runtime state available from it.
  //
  // Otherwise, use defaults for an unloaded story and send a request for the
  // story to start running (stories should start running by default).
  fuchsia::modular::StoryState runtime_state = fuchsia::modular::StoryState::STOPPED;
  auto it = story_runtime_containers_.find(story_data.story_info().id());
  if (it != story_runtime_containers_.end()) {
    auto& container = it->second;
    runtime_state = container.controller_impl->runtime_state();
    container.current_data = CloneOptional(story_data);
    container.ResetInspect();
  } else {
    fuchsia::modular::StoryControllerPtr story_controller;
    GetController(story_id, story_controller.NewRequest());
    story_controller->RequestStart();
  }
  NotifyStoryWatchers(&story_data, runtime_state);
}

void StoryProviderImpl::OnStoryStorageDeleted(std::string story_id) {
  operation_queue_.Add(
      std::make_unique<StopStoryCall>(story_id, false /* skip_notifying_sessionshell */,
                                      &story_runtime_containers_, [this, story_id] {
                                        for (const auto& i : watchers_.ptrs()) {
                                          (*i)->OnDelete(story_id);
                                        }
                                      }));
}

void StoryProviderImpl::NotifyStoryWatchers(const fuchsia::modular::internal::StoryData* story_data,
                                            const fuchsia::modular::StoryState story_state) {
  if (!story_data) {
    return;
  }
  for (const auto& i : watchers_.ptrs()) {
    if (!story_data->has_story_info()) {
      continue;
    }
    (*i)->OnChange(StoryInfo2ToStoryInfo(story_data->story_info()), story_state,
                   fuchsia::modular::StoryVisibilityState::DEFAULT);
    (*i)->OnChange2(CloneStruct(story_data->story_info()), story_state,
                    fuchsia::modular::StoryVisibilityState::DEFAULT);
  }
}

void StoryProviderImpl::GetPresentation(
    std::string story_id, fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  presentation_provider_->GetPresentation(std::move(story_id), std::move(request));
}

void StoryProviderImpl::WatchVisualState(
    std::string story_id,
    fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher) {
  presentation_provider_->WatchVisualState(std::move(story_id), std::move(watcher));
}

fuchsia::modular::StoryInfo StoryProviderImpl::StoryInfo2ToStoryInfo(
    const fuchsia::modular::StoryInfo2& story_info_2) {
  fuchsia::modular::StoryInfo story_info;

  story_info.id = story_info_2.id();
  story_info.last_focus_time = story_info_2.last_focus_time();

  return story_info;
}

void StoryProviderImpl::StoryRuntimeContainer::InitializeInspect(
    std::string story_id, inspect::Node* session_inspect_node) {
  story_node = std::make_unique<inspect::Node>(session_inspect_node->CreateChild(story_id));
  ResetInspect();
}

void StoryProviderImpl::StoryRuntimeContainer::ResetInspect() {
  if (current_data->story_info().has_annotations()) {
    for (const fuchsia::modular::Annotation& annotation :
         current_data->story_info().annotations()) {
      std::string value_str = modular::annotations::ToInspect(*annotation.value.get());
      std::string key_with_prefix = "annotation: " + annotation.key;
      if (annotation_inspect_properties.find(key_with_prefix) !=
          annotation_inspect_properties.end()) {
        annotation_inspect_properties[key_with_prefix].Set(value_str);
      } else {
        annotation_inspect_properties.insert(std::pair<const std::string, inspect::StringProperty>(
            annotation.key, story_node->CreateString(key_with_prefix, value_str)));
      }
    }
  }
}

}  // namespace modular
