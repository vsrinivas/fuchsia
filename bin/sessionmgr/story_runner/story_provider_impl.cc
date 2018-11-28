// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story_runner/story_provider_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include <fuchsia/scenic/snapshot/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/array.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/random/uuid.h>
#include <lib/zx/time.h>

#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/sessionmgr/focus.h"
#include "peridot/bin/sessionmgr/presentation_provider.h"
#include "peridot/bin/sessionmgr/storage/constants_and_utils.h"
#include "peridot/bin/sessionmgr/storage/session_storage.h"
#include "peridot/bin/sessionmgr/storage/story_storage.h"
#include "peridot/bin/sessionmgr/story_runner/link_impl.h"
#include "peridot/bin/sessionmgr/story_runner/story_controller_impl.h"
#include "peridot/lib/common/names.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/fidl/proxy.h"
#include "peridot/lib/rapidjson/rapidjson.h"

// In tests prefetching mondrian saved ~30ms in story start up time.
#define PREFETCH_MONDRIAN 1

namespace modular {

constexpr char kSnapshotLoaderUrl[] = "snapshot";

class StoryProviderImpl::DeleteStoryCall : public Operation<> {
 public:
  using StoryRuntimesMap = std::map<std::string, struct StoryRuntimeContainer>;
  using PendingDeletion = std::pair<std::string, DeleteStoryCall*>;

  DeleteStoryCall(SessionStorage* session_storage, fidl::StringPtr story_id,
                  StoryRuntimesMap* const story_runtime_containers,
                  MessageQueueManager* const message_queue_manager,
                  const bool already_deleted, ResultCall result_call)
      : Operation("StoryProviderImpl::DeleteStoryCall", std::move(result_call)),
        session_storage_(session_storage),
        story_id_(story_id),
        story_runtime_containers_(story_runtime_containers),
        message_queue_manager_(message_queue_manager),
        already_deleted_(already_deleted) {}

 private:
  void Run() override {
    FlowToken flow{this};

    if (already_deleted_) {
      Teardown(flow);
    } else {
      session_storage_->DeleteStory(story_id_)->WeakThen(
          GetWeakPtr(), [this, flow] { Teardown(flow); });
    }
  }

  void Teardown(FlowToken flow) {
    auto i = story_runtime_containers_->find(story_id_);
    if (i == story_runtime_containers_->end()) {
      return;
    }

    FXL_DCHECK(i->second.controller_impl != nullptr);
    i->second.controller_impl->StopForDelete([this, flow] { Erase(flow); });
  }

  void Erase(FlowToken flow) {
    // Here we delete the instance from whose operation a result callback was
    // received. Thus we must assume that the callback returns to a method of
    // the instance. If we delete the instance right here, |this| would be
    // deleted not just for the remainder of this function here, but also for
    // the remainder of all functions above us in the callstack, including
    // functions that run as methods of other objects owned by |this| or
    // provided to |this|. To avoid such problems, the delete is invoked
    // through the run loop.
    async::PostTask(async_get_default_dispatcher(), [this, flow] {
      story_runtime_containers_->erase(story_id_);
      message_queue_manager_->DeleteNamespace(
          EncodeModuleComponentNamespace(story_id_), [flow] {});

      // TODO(mesch): We must delete the story page too. MI4-1002
    });
  }

 private:
  SessionStorage* const session_storage_;  // Not owned.
  const fidl::StringPtr story_id_;
  StoryRuntimesMap* const story_runtime_containers_;
  MessageQueueManager* const message_queue_manager_;
  const bool already_deleted_;  // True if called from OnChange();

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteStoryCall);
};

// Loads a StoryRuntimeContainer object so that the given story is ready to be
// run.
class StoryProviderImpl::LoadStoryRuntimeCall
    : public Operation<StoryRuntimeContainer*> {
 public:
  LoadStoryRuntimeCall(StoryProviderImpl* const story_provider_impl,
                       SessionStorage* const session_storage,
                       fidl::StringPtr story_id, ResultCall result_call)
      : Operation("StoryProviderImpl::LoadStoryRuntimeCall",
                  std::move(result_call)),
        story_provider_impl_(story_provider_impl),
        session_storage_(session_storage),
        story_id_(story_id) {}

 private:
  void Run() override {
    FlowToken flow{this, &story_controller_container_};

    // Use the existing controller, if possible.
    // This won't race against itself because it's managed by an operation
    // queue.
    auto i = story_provider_impl_->story_runtime_containers_.find(story_id_);
    if (i != story_provider_impl_->story_runtime_containers_.end()) {
      story_controller_container_ = &i->second;
      return;
    }

    session_storage_->GetStoryData(story_id_)->WeakThen(
        GetWeakPtr(),
        [this, flow](fuchsia::modular::internal::StoryDataPtr story_data) {
          if (!story_data) {
            return;
            // Operation finishes since |flow| goes out of scope.
          }
          story_info_ = CloneOptional(story_data->story_info);
          Cont(flow);
        });
  }

  void Cont(FlowToken flow) {
    session_storage_->GetStoryStorage(story_id_)->WeakThen(
        GetWeakPtr(),
        [this, flow](std::unique_ptr<StoryStorage> story_storage) {
          struct StoryRuntimeContainer container;
          container.storage = std::move(story_storage);
          container.controller_impl = std::make_unique<StoryControllerImpl>(
              story_id_, session_storage_, container.storage.get(),
              story_provider_impl_);
          container.current_info = std::move(story_info_);
          container.entity_provider =
              std::make_unique<StoryEntityProvider>(container.storage.get());
          auto it = story_provider_impl_->story_runtime_containers_.emplace(
              story_id_, std::move(container));
          story_controller_container_ = &it.first->second;
        });
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned
  SessionStorage* const session_storage_;         // not owned
  const fidl::StringPtr story_id_;
  fuchsia::modular::StoryInfoPtr story_info_;

  StoryRuntimeContainer* story_controller_container_ = nullptr;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LoadStoryRuntimeCall);
};

class StoryProviderImpl::StopAllStoriesCall : public Operation<> {
 public:
  StopAllStoriesCall(StoryProviderImpl* const story_provider_impl,
                     ResultCall result_call)
      : Operation("StoryProviderImpl::StopAllStoriesCall",
                  std::move(result_call)),
        story_provider_impl_(story_provider_impl) {}

 private:
  void Run() override {
    FlowToken flow{this};

    for (auto& it : story_provider_impl_->story_runtime_containers_) {
      // Each callback has a copy of |flow| which only goes out-of-scope
      // once the story corresponding to |it| stops.
      //
      // TODO(mesch): If a DeleteCall is executing in front of
      // StopForTeardown(), then the StopCall in StopForTeardown() never
      // executes because the fuchsia::modular::StoryController instance is
      // deleted after the DeleteCall finishes. This will then block unless it
      // runs in a timeout.
      it.second.controller_impl->StopForTeardown(
          [this, story_id = it.first, flow] {
            // It is okay to erase story_id because story provider binding has
            // been closed and this callback cannot be invoked synchronously.
            story_provider_impl_->story_runtime_containers_.erase(story_id);
          });
    }
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(StopAllStoriesCall);
};

class StoryProviderImpl::StopStoryShellCall : public Operation<> {
 public:
  StopStoryShellCall(StoryProviderImpl* const story_provider_impl,
                     ResultCall result_call)
      : Operation("StoryProviderImpl::StopStoryShellCall",
                  std::move(result_call)),
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
          kBasicTimeout,
          [branch] { std::unique_ptr<FlowToken> flow = branch.Continue(); });
    }
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(StopStoryShellCall);
};

class StoryProviderImpl::GetStoryEntityProviderCall
    : public Operation<StoryEntityProvider*> {
 public:
  GetStoryEntityProviderCall(StoryProviderImpl* const story_provider_impl,
                             const std::string& story_id,
                             ResultCall result_call)
      : Operation("StoryProviderImpl::GetStoryEntityProviderCall",
                  std::move(result_call)),
        story_provider_impl_(story_provider_impl),
        story_id_(story_id) {}

 private:
  void Run() override {
    FlowToken flow{this, &story_entity_provider_};

    operation_queue_.Add(new LoadStoryRuntimeCall(
        story_provider_impl_, story_provider_impl_->session_storage_, story_id_,
        [this, flow](StoryRuntimeContainer* story_controller_container) {
          if (story_controller_container) {
            story_entity_provider_ =
                story_controller_container->entity_provider.get();
          }
        }));
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned

  // The returned story entity provider.
  StoryEntityProvider* story_entity_provider_ = nullptr;

  fuchsia::modular::StoryInfoPtr story_info_;

  OperationQueue operation_queue_;

  std::string story_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetStoryEntityProviderCall);
};

StoryProviderImpl::StoryProviderImpl(
    Environment* const user_environment, std::string device_id,
    SessionStorage* const session_storage,
    fuchsia::modular::AppConfig story_shell,
    const ComponentContextInfo& component_context_info,
    fuchsia::modular::FocusProviderPtr focus_provider,
    fuchsia::modular::UserIntelligenceProvider* const
        user_intelligence_provider,
    fuchsia::modular::ModuleResolver* const module_resolver,
    EntityProviderRunner* const entity_provider_runner,
    modular::ModuleFacetReader* const module_facet_reader,
    PresentationProvider* const presentation_provider,
    fuchsia::ui::viewsv1::ViewSnapshotPtr view_snapshot, const bool test)
    : user_environment_(user_environment),
      session_storage_(session_storage),
      device_id_(std::move(device_id)),
      story_shell_(std::move(story_shell)),
      test_(test),
      component_context_info_(component_context_info),
      user_intelligence_provider_(user_intelligence_provider),
      module_resolver_(module_resolver),
      entity_provider_runner_(entity_provider_runner),
      module_facet_reader_(module_facet_reader),
      presentation_provider_(presentation_provider),
      focus_provider_(std::move(focus_provider)),
      focus_watcher_binding_(this),
      view_snapshot_(std::move(view_snapshot)),
      weak_factory_(this) {
  session_storage_->set_on_story_deleted(
      [weak_ptr = weak_factory_.GetWeakPtr()](fidl::StringPtr story_id) {
        if (!weak_ptr)
          return;
        weak_ptr->OnStoryStorageDeleted(std::move(story_id));
      });
  session_storage_->set_on_story_updated(
      [weak_ptr = weak_factory_.GetWeakPtr()](
          fidl::StringPtr story_id,
          fuchsia::modular::internal::StoryData story_data) {
        if (!weak_ptr)
          return;
        weak_ptr->OnStoryStorageUpdated(std::move(story_id),
                                        std::move(story_data));
      });

  focus_provider_->Watch(focus_watcher_binding_.NewBinding());
  if (!test_) {
    // As an optimization, since app startup time is long, we optimistically
    // load a story shell instance even if there are no stories that need it
    // yet. This can reduce the time to first frame.
    MaybeLoadStoryShellDelayed();
  }
}

StoryProviderImpl::~StoryProviderImpl() = default;

void StoryProviderImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::StoryProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void StoryProviderImpl::StopAllStories(const std::function<void()>& callback) {
  operation_queue_.Add(new StopAllStoriesCall(this, callback));
}

void StoryProviderImpl::Teardown(const std::function<void()>& callback) {
  // Closing all binding to this instance ensures that no new messages come
  // in, though previous messages need to be processed. The stopping of
  // stories is done on |operation_queue_| since that must strictly happen
  // after all pending messgages have been processed.
  bindings_.CloseAll();
  operation_queue_.Add(new StopAllStoriesCall(this, [] {}));
  operation_queue_.Add(new StopStoryShellCall(this, callback));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::Watch(
    fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher) {
  auto watcher_ptr = watcher.Bind();
  for (const auto& item : story_runtime_containers_) {
    const auto& container = item.second;
    watcher_ptr->OnChange(CloneStruct(*container.current_info),
                          container.controller_impl->GetStoryState(),
                          container.controller_impl->GetStoryVisibilityState());
  }
  watchers_.AddInterfacePtr(std::move(watcher_ptr));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::WatchActivity(
    fidl::InterfaceHandle<fuchsia::modular::StoryActivityWatcher> watcher) {
  auto watcher_ptr = watcher.Bind();
  for (const auto& item : story_runtime_containers_) {
    const auto& container = item.second;
    watcher_ptr->OnStoryActivityChange(
        container.controller_impl->GetStoryId(),
        container.controller_impl->GetOngoingActivities());
  }
  activity_watchers_.AddInterfacePtr(std::move(watcher_ptr));
}

std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>>
StoryProviderImpl::StartStoryShell(
    fidl::StringPtr story_id,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner> request) {
  MaybeLoadStoryShell();

  auto app_client = std::move(preloaded_story_shell_app_);

  // TODO(SCN-1019): This is a temporary hack to cache the endpoint ID of the
  // view so that framework can make snapshot requests.
  view_endpoints_[story_id] = fsl::GetKoid(request.channel().get());

  fuchsia::ui::viewsv1::ViewProviderPtr view_provider;
  app_client->services().ConnectToService(view_provider.NewRequest());
  view_provider->CreateView(std::move(request), nullptr);

  // Kickoff another fuchsia::modular::StoryShell, to make it faster for next
  // story. We optimize even further by delaying the loading of the next story
  // shell instance by waiting a few seconds.
  if (!test_) {
    MaybeLoadStoryShellDelayed();
  }

  return app_client;
}

void StoryProviderImpl::MaybeLoadStoryShellDelayed() {
#if PREFETCH_MONDRIAN
  async::PostDelayedTask(
      async_get_default_dispatcher(),
      [weak_this = weak_factory_.GetWeakPtr()] {
        if (weak_this) {
          weak_this->operation_queue_.Add(new SyncCall([weak_this] {
            if (weak_this) {
              weak_this->MaybeLoadStoryShell();
            }
          }));
        }
      },
      zx::sec(5));
#endif
}

void StoryProviderImpl::MaybeLoadStoryShell() {
  if (preloaded_story_shell_app_) {
    return;
  }

  preloaded_story_shell_app_ =
      std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
          user_environment_->GetLauncher(), CloneStruct(story_shell_));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::DeleteStory(fidl::StringPtr story_id,
                                    DeleteStoryCallback callback) {
  operation_queue_.Add(new DeleteStoryCall(
      session_storage_, story_id, &story_runtime_containers_,
      component_context_info_.message_queue_manager,
      false /* already_deleted */, callback));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStoryInfo(fidl::StringPtr story_id,
                                     GetStoryInfoCallback callback) {
  auto on_run = Future<>::Create("StoryProviderImpl.GetStoryInfo.on_run");
  auto done =
      on_run
          ->AsyncMap([this, story_id] {
            return session_storage_->GetStoryData(story_id);
          })
          ->Map([](fuchsia::modular::internal::StoryDataPtr story_data)
                    -> fuchsia::modular::StoryInfoPtr {
            if (!story_data) {
              return nullptr;
            }
            return fidl::MakeOptional(std::move(story_data->story_info));
          });
  operation_queue_.Add(WrapFutureAsOperation("StoryProviderImpl::GetStoryInfo",
                                             on_run, done, callback));
}

// Called by StoryControllerImpl on behalf of ModuleContextImpl
void StoryProviderImpl::RequestStoryFocus(fidl::StringPtr story_id) {
  FXL_LOG(INFO) << "RequestStoryFocus() " << story_id;
  focus_provider_->Request(story_id);
}

void StoryProviderImpl::NotifyStoryStateChange(
    fidl::StringPtr story_id, const fuchsia::modular::StoryState story_state,
    const fuchsia::modular::StoryVisibilityState story_visibility_state) {
  auto on_run =
      Future<>::Create("StoryProviderImpl.NotifyStoryStateChange.on_run");
  auto done = on_run
                  ->AsyncMap([this, story_id] {
                    return session_storage_->GetStoryData(story_id);
                  })
                  ->Then([this, story_id, story_state, story_visibility_state](
                             fuchsia::modular::internal::StoryDataPtr data) {
                    auto it = story_runtime_containers_.find(story_id);
                    if (it == story_runtime_containers_.end()) {
                      // If this call arrives while DeleteStory() is in
                      // progress, the story controller might already be gone
                      // from here.
                      return;
                    }
                    NotifyStoryWatchers(data.get(), story_state,
                                        story_visibility_state);
                  });
  std::function<void()> callback = [] {};
  operation_queue_.Add(WrapFutureAsOperation(
      "StoryProviderImpl::NotifyStoryStateChange", on_run, done, callback));
}

void StoryProviderImpl::NotifyStoryActivityChange(
    fidl::StringPtr story_id,
    fidl::VectorPtr<fuchsia::modular::OngoingActivityType> ongoing_activities) {
  for (const auto& i : activity_watchers_.ptrs()) {
    (*i)->OnStoryActivityChange(story_id, std::move(ongoing_activities));
  }
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetController(
    fidl::StringPtr story_id,
    fidl::InterfaceRequest<fuchsia::modular::StoryController> request) {
  operation_queue_.Add(new LoadStoryRuntimeCall(
      this, session_storage_, story_id,
      fxl::MakeCopyable(
          [request = std::move(request)](
              StoryRuntimeContainer* story_controller_container) mutable {
            if (story_controller_container) {
              story_controller_container->controller_impl->Connect(
                  std::move(request));
            }
          })));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::GetStories(
    fidl::InterfaceHandle<fuchsia::modular::StoryProviderWatcher> watcher,
    PreviousStoriesCallback callback) {
  auto watcher_ptr = watcher.Bind();
  auto on_run = Future<>::Create("StoryProviderImpl.GetStories.on_run");
  auto done =
      on_run->AsyncMap([this] { return session_storage_->GetAllStoryData(); })
          ->Map(fxl::MakeCopyable(
              [this, watcher_ptr = std::move(watcher_ptr)](
                  fidl::VectorPtr<fuchsia::modular::internal::StoryData>
                      all_story_data) mutable {
                FXL_DCHECK(all_story_data);
                auto result =
                    fidl::VectorPtr<fuchsia::modular::StoryInfo>::New(0);

                for (auto& story_data : *all_story_data) {
                  if (!story_data.story_options.kind_of_proto_story) {
                    result.push_back(std::move(story_data.story_info));
                  }
                }

                if (watcher_ptr) {
                  watchers_.AddInterfacePtr(std::move(watcher_ptr));
                }
                return result;
              }));

  operation_queue_.Add(WrapFutureAsOperation("StoryProviderImpl::GetStories",
                                             on_run, done, callback));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::PreviousStories(PreviousStoriesCallback callback) {
  auto on_run = Future<>::Create("StoryProviderImpl.PreviousStories.on_run");
  auto done =
      on_run->AsyncMap([this] { return session_storage_->GetAllStoryData(); })
          ->Map([](fidl::VectorPtr<fuchsia::modular::internal::StoryData>
                       all_story_data) {
            FXL_DCHECK(all_story_data);
            auto result = fidl::VectorPtr<fuchsia::modular::StoryInfo>::New(0);

            for (auto& story_data : *all_story_data) {
              if (!story_data.story_options.kind_of_proto_story) {
                result.push_back(std::move(story_data.story_info));
              }
            }
            return result;
          });
  operation_queue_.Add(WrapFutureAsOperation(
      "StoryProviderImpl::PreviousStories", on_run, done, callback));
}

// |fuchsia::modular::StoryProvider|
void StoryProviderImpl::RunningStories(RunningStoriesCallback callback) {
  auto on_run = Future<>::Create("StoryProviderImpl.RunningStories.on_run");
  auto done = on_run->Map([this]() {
    auto stories = fidl::VectorPtr<fidl::StringPtr>::New(0);
    for (const auto& impl_container : story_runtime_containers_) {
      if (impl_container.second.controller_impl->IsRunning()) {
        stories.push_back(impl_container.second.controller_impl->GetStoryId());
      }
    }
    return stories;
  });
  operation_queue_.Add(WrapFutureAsOperation(
      "StoryProviderImpl::RunningStories", on_run, done, callback));
}

void StoryProviderImpl::OnStoryStorageUpdated(
    fidl::StringPtr story_id,
    fuchsia::modular::internal::StoryData story_data) {
  // HACK(jimbe) We don't have the page and it's expensive to get it, so
  // just mark it as STOPPED. We know it's not running or we'd have a
  // fuchsia::modular::StoryController.
  //
  // If we have a StoryControllerImpl for this story id, update our cached
  // fuchsia::modular::StoryInfo.
  fuchsia::modular::StoryState state = fuchsia::modular::StoryState::STOPPED;
  fuchsia::modular::StoryVisibilityState visibility_state =
      fuchsia::modular::StoryVisibilityState::DEFAULT;
  auto i = story_runtime_containers_.find(story_data.story_info.id);
  if (i != story_runtime_containers_.end()) {
    state = i->second.controller_impl->GetStoryState();
    visibility_state = i->second.controller_impl->GetStoryVisibilityState();
    i->second.current_info = CloneOptional(story_data.story_info);
  }

  NotifyStoryWatchers(&story_data, state, visibility_state);
}

void StoryProviderImpl::OnStoryStorageDeleted(fidl::StringPtr story_id) {
  // NOTE: DeleteStoryCall is used here, as well as in DeleteStory(). In this
  // case, either another device deleted the story, or we did and the Ledger
  // is now notifying us. In this case, we pass |already_deleted = true| so
  // that we don't ask to delete the story data again.
  operation_queue_.Add(new DeleteStoryCall(
      session_storage_, story_id, &story_runtime_containers_,
      component_context_info_.message_queue_manager, true /* already_deleted */,
      [this, story_id] {
        for (const auto& i : watchers_.ptrs()) {
          (*i)->OnDelete(story_id);
        }
      }));
}

// |fuchsia::modular::FocusWatcher|
void StoryProviderImpl::OnFocusChange(fuchsia::modular::FocusInfoPtr info) {
  operation_queue_.Add(
      new SyncCall(fxl::MakeCopyable([this, info = std::move(info)]() {
        if (info->device_id.get() != device_id_) {
          return;
        }

        if (info->focused_story_id.is_null()) {
          return;
        }

        auto i = story_runtime_containers_.find(info->focused_story_id.get());
        if (i == story_runtime_containers_.end()) {
          FXL_LOG(ERROR) << "Story controller not found for focused story "
                         << info->focused_story_id;
          return;
        }

        // Last focus time is recorded in the ledger, and story provider
        // watchers are notified through watching SessionStorage.
        auto on_run =
            Future<>::Create("StoryProviderImpl.OnFocusChange.on_run");
        auto done = on_run->AsyncMap([this, story_id = info->focused_story_id] {
          return session_storage_->UpdateLastFocusedTimestamp(
              story_id, zx_clock_get(ZX_CLOCK_UTC));
        });
        std::function<void()> callback = [] {};
        operation_queue_.Add(WrapFutureAsOperation(
            "StoryProviderImpl::OnFocusChange", on_run, done, callback));
      })));
}

void StoryProviderImpl::NotifyStoryWatchers(
    const fuchsia::modular::internal::StoryData* const story_data,
    const fuchsia::modular::StoryState story_state,
    const fuchsia::modular::StoryVisibilityState story_visibility_state) {
  if (story_data->story_options.kind_of_proto_story) {
    return;
  }
  for (const auto& i : watchers_.ptrs()) {
    (*i)->OnChange(CloneStruct(story_data->story_info), story_state,
                   story_visibility_state);
  }
}

void StoryProviderImpl::CreateEntity(
    const std::string& story_id, fidl::StringPtr type,
    fuchsia::mem::Buffer data,
    fidl::InterfaceRequest<fuchsia::modular::Entity> entity_request,
    std::function<void(std::string /* entity_reference */)> callback) {
  operation_queue_.Add(new GetStoryEntityProviderCall(
      this, story_id,
      fxl::MakeCopyable([this, type, story_id, data = std::move(data),
                         callback = std::move(callback),
                         entity_request = std::move(entity_request)](
                            StoryEntityProvider* entity_provider) mutable {
        // Once the entity provider for the given story is available, create the
        // entity.
        entity_provider->CreateEntity(
            type, std::move(data),
            fxl::MakeCopyable([this, entity_request = std::move(entity_request),
                               callback = std::move(callback),
                               story_id](std::string cookie) mutable {
              if (cookie.empty()) {
                // Return nullptr to indicate the entity creation failed.
                callback(nullptr);
                return;
              }

              std::string entity_reference =
                  entity_provider_runner_->CreateStoryEntityReference(story_id,
                                                                      cookie);

              // Once the entity reference has been created, it can be
              // used to connect the entity request.
              fuchsia::modular::EntityResolverPtr resolver;
              entity_provider_runner_->ConnectEntityResolver(
                  resolver.NewRequest());
              resolver->ResolveEntity(entity_reference,
                                      std::move(entity_request));

              callback(entity_reference);
            }));
      })));
}

void StoryProviderImpl::ConnectToStoryEntityProvider(
    const std::string& story_id,
    fidl::InterfaceRequest<fuchsia::modular::EntityProvider>
        entity_provider_request) {
  operation_queue_.Add(new GetStoryEntityProviderCall(
      this, story_id,
      fxl::MakeCopyable(
          [entity_provider_request = std::move(entity_provider_request)](
              StoryEntityProvider* entity_provider) mutable {
            entity_provider->Connect(std::move(entity_provider_request));
          })));
}

void StoryProviderImpl::GetPresentation(
    fidl::StringPtr story_id,
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  presentation_provider_->GetPresentation(std::move(story_id),
                                          std::move(request));
}

void StoryProviderImpl::WatchVisualState(
    fidl::StringPtr story_id,
    fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher) {
  presentation_provider_->WatchVisualState(std::move(story_id),
                                           std::move(watcher));
}

void StoryProviderImpl::TakeSnapshot(
    fidl::StringPtr story_id,
    fit::function<void(fuchsia::mem::Buffer)> callback) {
  auto it = view_endpoints_.find(story_id);
  if (it != view_endpoints_.end()) {
    view_snapshot_->TakeSnapshot(it->second, [callback = std::move(callback)](
                                                 fuchsia::mem::Buffer buffer) {
      callback(std::move(buffer));
    });
  } else {
    callback(fuchsia::mem::Buffer{});
  }
}

void StoryProviderImpl::StartSnapshotLoader(
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    fidl::InterfaceRequest<fuchsia::scenic::snapshot::Loader> loader_request) {
  if (!snapshot_loader_app_) {
    fuchsia::modular::AppConfig snapshot_loader_config;
    snapshot_loader_config.url = kSnapshotLoaderUrl;

    snapshot_loader_app_ =
        std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
            user_environment_->GetLauncher(),
            std::move(snapshot_loader_config));
  }

  fuchsia::sys::ServiceProviderPtr service_provider;
  fuchsia::ui::viewsv1::ViewProviderPtr view_provider;
  snapshot_loader_app_->services().ConnectToService(view_provider.NewRequest());
  view_provider->CreateView(std::move(view_owner_request),
                            service_provider.NewRequest());

  service_provider->ConnectToService(fuchsia::scenic::snapshot::Loader::Name_,
                                     loader_request.TakeChannel());
}

}  // namespace modular
