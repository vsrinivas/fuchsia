// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_provider_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include "lib/app/cpp/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/lib/fidl/proxy.h"
#include "apps/modular/lib/ledger/operations.h"
#include "apps/modular/lib/ledger/storage.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/services/module/link_path.fidl.h"
#include "apps/modular/src/story_runner/link_impl.h"
#include "apps/modular/src/story_runner/story_controller_impl.h"
#include "apps/modular/src/user_runner/focus.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace modular {

namespace {

// Serialization and deserialization of StoryData and StoryInfo to and
// from JSON.

void XdrStoryInfo(XdrContext* const xdr, StoryInfo* const data) {
  // TODO(jimbe) Remove error handler after 2017-08-01
  xdr->ReadErrorHandler(
         [&data] { data->last_focus_time = mx_time_get(MX_CLOCK_UTC); })
      ->Field("last_focus_time", &data->last_focus_time);
  xdr->Field("url", &data->url);
  xdr->Field("id", &data->id);
  xdr->Field("extra", &data->extra);
}

void XdrStoryData(XdrContext* const xdr, StoryData* const data) {
  xdr->Field("story_info", &data->story_info, XdrStoryInfo);
  xdr->Field("story_page_id", &data->story_page_id);
}

void MakeGetStoryDataCall(OperationContainer* const container,
                          ledger::Page* const page,
                          const fidl::String& story_id,
                          std::function<void(StoryDataPtr)> result_call) {
  new ReadDataCall<StoryData>(container, page, MakeStoryKey(story_id),
                              true /* not_found_is_ok */, XdrStoryData,
                              std::move(result_call));
};

void MakeWriteStoryDataCall(OperationContainer* const container,
                            ledger::Page* const page,
                            StoryDataPtr story_data,
                            std::function<void()> result_call) {
  new WriteDataCall<StoryData>(
      container, page, MakeStoryKey(story_data->story_info->id), XdrStoryData,
      std::move(story_data), std::move(result_call));
};

}  // namespace

class StoryProviderImpl::MutateStoryDataCall : Operation<> {
 public:
  MutateStoryDataCall(OperationContainer* const container,
                      ledger::Page* const page,
                      const fidl::String& story_id,
                      std::function<bool(StoryData* story_data)> mutate,
                      ResultCall result_call)
      : Operation("StoryProviderImpl::MutateStoryDataCall",
                  container,
                  std::move(result_call)),
        page_(page),
        story_id_(story_id),
        mutate_(std::move(mutate)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    MakeGetStoryDataCall(&operation_queue_, page_, story_id_,
                         [this, flow](StoryDataPtr story_data) {
                           if (!story_data) {
                             // If the story doesn't exist, it was deleted and
                             // we must not bring it back.
                             return;
                           }
                           if (!mutate_(story_data.get())) {
                             // If no mutation happened, we're done.
                             return;
                           }

                           MakeWriteStoryDataCall(&operation_queue_, page_,
                                                  std::move(story_data),
                                                  [flow] {});
                         });
  }

  ledger::Page* const page_;  // not owned
  const fidl::String story_id_;
  std::function<bool(StoryData* story_data)> mutate_;

  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MutateStoryDataCall);
};

// 1. Create a page for the new story.
// 2. Create a new StoryData structure pointing to this new page and save it
//    to the root page.
// 3. Write a copy of the current context to the story page.
// 4. Returns the Story ID of the newly created story.
class StoryProviderImpl::CreateStoryCall : Operation<fidl::String> {
 public:
  CreateStoryCall(OperationContainer* const container,
                  ledger::Ledger* const ledger,
                  ledger::Page* const root_page,
                  StoryProviderImpl* const story_provider_impl,
                  const fidl::String& url,
                  FidlStringMap extra_info,
                  fidl::String root_json,
                  ResultCall result_call)
      : Operation("StoryProviderImpl::CreateStoryCall",
                  container,
                  std::move(result_call)),
        ledger_(ledger),
        root_page_(root_page),
        story_provider_impl_(story_provider_impl),
        url_(url),
        extra_info_(std::move(extra_info)),
        root_json_(root_json) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &story_id_};

    ledger_->GetPage(
        nullptr, story_page_.NewRequest(), [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "CreateStoryCall()"
                           << " Ledger.GetPage() " << status;
            return;
          }

          story_page_->GetId([this, flow](fidl::Array<uint8_t> id) {
            story_page_id_ = std::move(id);

            // TODO(security), cf. FW-174. This ID is exposed in
            // public services such as
            // StoryProvider.PreviousStories(),
            // StoryController.GetInfo(),
            // ModuleContext.GetStoryId(). We need to ensure this
            // doesn't expose internal information by being a
            // page ID.
            story_id_ = to_hex_string(story_page_id_);

            story_data_ = StoryData::New();
            story_data_->story_page_id = story_page_id_.Clone();
            story_data_->story_info = StoryInfo::New();
            auto* const story_info = story_data_->story_info.get();
            story_info->url = url_;
            story_info->id = story_id_;
            story_info->last_focus_time = mx_time_get(MX_CLOCK_UTC);
            story_info->extra = std::move(extra_info_);
            story_info->extra.mark_non_null();

            MakeWriteStoryDataCall(&operation_queue_, root_page_,
                                   std::move(story_data_),
                                   [this, flow] { Cont1(flow); });
          });
        });
  }

  void Cont1(FlowToken flow) {
    controller_ = std::make_unique<StoryControllerImpl>(
        story_id_, story_provider_impl_->ledger_client_,
        std::move(story_page_id_), story_provider_impl_);
    controller_->AddForCreate(kRootModuleName, url_, kRootLink, root_json_,
                              [this, flow] { Cont2(flow); });
  }

  void Cont2(FlowToken flow) {
    controller_->Log(story_provider_impl_->MakeLogEntry(StorySignal::CREATED));

    // We ensure that everything has been written to the story page before this
    // operation is done.
    controller_->Sync(
        [this, flow] { story_provider_impl_->NotifyImportanceWatchers(); });
  }

  ledger::Ledger* const ledger_;                  // not owned
  ledger::Page* const root_page_;                 // not owned
  StoryProviderImpl* const story_provider_impl_;  // not owned
  const fidl::String module_name_;
  const fidl::String url_;
  FidlStringMap extra_info_;
  fidl::String root_json_;

  ledger::PagePtr story_page_;
  StoryDataPtr story_data_;
  std::unique_ptr<StoryControllerImpl> controller_;

  fidl::Array<uint8_t> story_page_id_;
  fidl::String story_id_;  // This is the result of the Operation.

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CreateStoryCall);
};

class StoryProviderImpl::DeleteStoryCall : Operation<> {
 public:
  using StoryControllerImplMap =
      std::unordered_map<std::string, struct StoryControllerImplContainer>;
  using PendingDeletion = std::pair<std::string, DeleteStoryCall*>;

  DeleteStoryCall(OperationContainer* const container,
                  ledger::Page* const page,
                  const fidl::String& story_id,
                  StoryControllerImplMap* const story_controller_impls,
                  MessageQueueManager* const message_queue_manager,
                  const bool already_deleted,
                  ResultCall result_call)
      : Operation("StoryProviderImpl::DeleteStoryCall",
                  container,
                  std::move(result_call)),
        page_(page),
        story_id_(story_id),
        story_controller_impls_(story_controller_impls),
        message_queue_manager_(message_queue_manager),
        already_deleted_(already_deleted) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    if (already_deleted_) {
      Teardown(flow);

    } else {
      page_->Delete(to_array(MakeStoryKey(story_id_)),
                    [this, flow](ledger::Status status) {
                      // Deleting a key that doesn't exist is OK, not
                      // KEY_NOT_FOUND.
                      if (status != ledger::Status::OK) {
                        FTL_LOG(ERROR) << "DeleteStoryCall() " << story_id_
                                       << " Page.Delete() " << status;
                      }

                      Teardown(flow);
                    });
    }
  }

  void Teardown(FlowToken flow) {
    auto i = story_controller_impls_->find(story_id_);
    if (i == story_controller_impls_->end()) {
      return;
    }

    FTL_DCHECK(i->second.impl != nullptr);
    i->second.impl->StopForDelete([this, flow] { Erase(flow); });
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
    mtl::MessageLoop::GetCurrent()->task_runner()->PostTask([this, flow] {
      story_controller_impls_->erase(story_id_);
      message_queue_manager_->DeleteNamespace(
          EncodeModuleComponentNamespace(story_id_), [flow] {});

      // TODO(mesch): We must delete the story page too.
    });
  }

 private:
  ledger::Page* const page_;  // not owned
  const fidl::String story_id_;
  StoryControllerImplMap* const story_controller_impls_;
  MessageQueueManager* const message_queue_manager_;
  const bool already_deleted_;  // True if called from OnChange();

  FTL_DISALLOW_COPY_AND_ASSIGN(DeleteStoryCall);
};

// 1. Ensure that the story data in the root page isn't dirty due to a crash
// 2. Retrieve the page specific to this story.
// 3. Return a controller for this story that contains the page pointer.
class StoryProviderImpl::GetControllerCall : Operation<> {
 public:
  using StoryControllerImplMap =
      std::unordered_map<std::string, struct StoryControllerImplContainer>;

  GetControllerCall(OperationContainer* const container,
                    ledger::Page* const page,
                    StoryProviderImpl* const story_provider_impl,
                    StoryControllerImplMap* const story_controller_impls,
                    const fidl::String& story_id,
                    fidl::InterfaceRequest<StoryController> request)
      : Operation("StoryProviderImpl::GetControllerCall", container, [] {}),
        page_(page),
        story_provider_impl_(story_provider_impl),
        story_controller_impls_(story_controller_impls),
        story_id_(story_id),
        request_(std::move(request)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    // Use the existing controller, if possible.
    auto i = story_controller_impls_->find(story_id_);
    if (i != story_controller_impls_->end()) {
      i->second.impl->Connect(std::move(request_));
      return;
    }

    MakeGetStoryDataCall(&operation_queue_, page_, story_id_,
                         [this, flow](StoryDataPtr story_data) {
                           if (story_data) {
                             story_data_ = std::move(story_data);
                             Cont1(flow);
                           }
                         });
  }

  void Cont1(FlowToken flow) {
    struct StoryControllerImplContainer container;
    container.impl = std::make_unique<StoryControllerImpl>(
        story_id_, story_provider_impl_->ledger_client_,
        story_data_->story_page_id.Clone(), story_provider_impl_);
    container.impl->Connect(std::move(request_));
    container.current_info = story_data_->story_info.Clone();
    story_controller_impls_->emplace(story_id_, std::move(container));
  }

  ledger::Page* const page_;                      // not owned
  StoryProviderImpl* const story_provider_impl_;  // not owned
  StoryControllerImplMap* const story_controller_impls_;
  const fidl::String story_id_;
  fidl::InterfaceRequest<StoryController> request_;

  StoryDataPtr story_data_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetControllerCall);
};

class StoryProviderImpl::TeardownCall : Operation<> {
 public:
  TeardownCall(OperationContainer* const container,
               StoryProviderImpl* const story_provider_impl,
               ResultCall result_call)
      : Operation("StoryProviderImpl::TeardownCall",
                  container,
                  std::move(result_call)),
        story_provider_impl_(story_provider_impl) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    for (auto& it : story_provider_impl_->story_controller_impls_) {
      // Each callback has a copy of |flow| which only goes out-of-scope once
      // the story corresponding to |it| stops.
      //
      // TODO(mesch): If a DeleteCall is executing in front of
      // StopForTeardown(), then the StopCall in StopForTeardown() never
      // executes because the StoryController instance is deleted after the
      // DeleteCall finishes. This will then block unless it runs in a timeout.
      it.second.impl->StopForTeardown([ this, story_id = it.first, flow ] {
        // It is okay to erase story_id because story provider binding has been
        // closed and this callback cannot be invoked synchronously.
        story_provider_impl_->story_controller_impls_.erase(story_id);
      });
    }
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned

  FTL_DISALLOW_COPY_AND_ASSIGN(TeardownCall);
};

class StoryProviderImpl::GetImportanceCall : Operation<ImportanceMap> {
 public:
  GetImportanceCall(OperationContainer* const container,
                    StoryProviderImpl* const story_provider_impl,
                    ResultCall result_call)
      : Operation("StoryProviderImpl::GetImportanceCall",
                  container,
                  std::move(result_call)),
        story_provider_impl_(story_provider_impl) {
    importance_.mark_non_null();
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &importance_};

    for (auto& story : story_provider_impl_->story_controller_impls_) {
      story.second.impl->GetImportance(
          story_provider_impl_->context_handler_.values(),
          [ this, id = story.first, flow ](float importance) {
            importance_[id] = importance;
          });
    }
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned
  ImportanceMap importance_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetImportanceCall);
};

struct StoryProviderImpl::LinkPeer {
  std::unique_ptr<StoryStorageImpl> storage;
  std::unique_ptr<LinkImpl> link;
};

class StoryProviderImpl::GetLinkPeerCall : Operation<> {
 public:
  GetLinkPeerCall(OperationContainer* const container,
                  StoryProviderImpl* const impl,
                  const fidl::String& story_id,
                  fidl::Array<fidl::String> module_path,
                  const fidl::String& link_name,
                  fidl::InterfaceRequest<Link> request)
      : Operation("StoryProviderImpl::GetLinkPeerCall", container, [] {}),
        impl_(impl),
        story_id_(story_id),
        module_path_(std::move(module_path)),
        link_name_(link_name),
        request_(std::move(request)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};

    MakeGetStoryDataCall(&operation_queue_, impl_->page(), story_id_,
                         [this, flow](StoryDataPtr story_data) {
                           if (story_data) {
                             story_data_ = std::move(story_data);
                             Cont(flow);
                           }
                         });
  }

  void Cont(FlowToken flow) {
    auto link_peer = std::make_unique<LinkPeer>();

    link_peer->storage.reset(new StoryStorageImpl(impl_->ledger_client_,
                                                  story_data_->story_page_id.Clone()));
    auto* const storage = link_peer->storage.get();

    auto link_path = LinkPath::New();
    link_path->module_path = module_path_.Clone();
    link_path->link_name = link_name_;

    link_peer->link = std::make_unique<LinkImpl>(storage, std::move(link_path));
    link_peer->link->Connect(std::move(request_));

    impl_->link_peers_.emplace_back(std::move(link_peer));

    // TODO(mesch): Set an orphaned handler so that link peers get dropped
    // earlier than at logout.
  }

  StoryProviderImpl* const impl_;  // not owned
  const fidl::String story_id_;
  const fidl::Array<fidl::String> module_path_;
  const fidl::String link_name_;
  fidl::InterfaceRequest<Link> request_;

  StoryDataPtr story_data_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetLinkPeerCall);
};

StoryProviderImpl::StoryProviderImpl(
    Scope* const user_scope,
    std::string device_id,
    LedgerClient* const ledger_client,
    LedgerPageId root_page_id,
    AppConfigPtr story_shell,
    const ComponentContextInfo& component_context_info,
    FocusProviderPtr focus_provider,
    maxwell::IntelligenceServices* const intelligence_services,
    maxwell::UserIntelligenceProvider* const user_intelligence_provider)
    : PageClient("StoryProviderImpl", ledger_client,
                 std::move(root_page_id), kStoryKeyPrefix),
      user_scope_(user_scope),
      device_id_(std::move(device_id)),
      ledger_client_(ledger_client),
      story_shell_(std::move(story_shell)),
      component_context_info_(component_context_info),
      user_intelligence_provider_(user_intelligence_provider),
      context_handler_(intelligence_services),
      focus_provider_(std::move(focus_provider)),
      focus_watcher_binding_(this) {
  focus_provider_->Watch(focus_watcher_binding_.NewBinding());
  context_handler_.Watch([this] { OnContextChange(); });
  context_handler_.SelectTopics({kStoryImportanceContext});
  LoadStoryShell();
}

StoryProviderImpl::~StoryProviderImpl() = default;

void StoryProviderImpl::Connect(fidl::InterfaceRequest<StoryProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void StoryProviderImpl::Teardown(const std::function<void()>& callback) {
  // Closing all binding to this instance ensures that no new messages come
  // in, though previous messages need to be processed. The stopping of stories
  // is done on |operation_queue_| since that must strictly happen after all
  // pending messgages have been processed.
  bindings_.CloseAllBindings();
  new TeardownCall(&operation_queue_, this, callback);
}

// |StoryProvider|
void StoryProviderImpl::Watch(
    fidl::InterfaceHandle<StoryProviderWatcher> watcher) {
  auto watcher_ptr = StoryProviderWatcherPtr::Create(std::move(watcher));
  for (const auto& item : story_controller_impls_) {
    const auto& container = item.second;
    watcher_ptr->OnChange(container.current_info.Clone(),
                          container.impl->GetStoryState());
  }
  watchers_.AddInterfacePtr(std::move(watcher_ptr));
}

// |StoryProvider|
void StoryProviderImpl::Duplicate(
    fidl::InterfaceRequest<StoryProvider> request) {
  Connect(std::move(request));
}

app::ApplicationControllerPtr StoryProviderImpl::StartStoryShell(
    fidl::InterfaceHandle<StoryContext> story_context,
    fidl::InterfaceRequest<StoryShell> story_shell_request,
    fidl::InterfaceRequest<mozart::ViewOwner> view_request) {
  if (!preloaded_story_shell_) {
    LoadStoryShell();
  }

  auto preloaded_story_shell = std::move(preloaded_story_shell_);
  auto controller = std::move(preloaded_story_shell->story_shell_controller);
  auto services = std::move(preloaded_story_shell->story_shell_services);

  proxies_.Connect(std::move(preloaded_story_shell->story_shell_view),
                   std::move(view_request));

  StoryShellFactoryPtr story_shell_factory;
  ConnectToService(services.get(), story_shell_factory.NewRequest());

  story_shell_factory->Create(std::move(story_context),
                              std::move(story_shell_request));

  // Kickoff another StoryShell, to make it faster for next story. We optimize
  // even further by delaying the loading of the next story shell instance by
  // doing that on the operation queue.
  new SyncCall(&operation_queue_, [this] { LoadStoryShell(); });

  return controller;
}

void StoryProviderImpl::LoadStoryShell() {
  app::ApplicationControllerPtr story_shell_controller;
  app::ServiceProviderPtr story_shell_services;
  mozart::ViewOwnerPtr story_shell_view;

  auto story_shell_launch_info = app::ApplicationLaunchInfo::New();
  story_shell_launch_info->services = story_shell_services.NewRequest();
  story_shell_launch_info->url = story_shell_->url;
  story_shell_launch_info->arguments = story_shell_->args.Clone();
  user_scope_->GetLauncher()->CreateApplication(
      std::move(story_shell_launch_info), story_shell_controller.NewRequest());
  mozart::ViewProviderPtr view_provider;
  ConnectToService(story_shell_services.get(), view_provider.NewRequest());
  // CreateView must be called in order to get the Flutter application to run
  view_provider->CreateView(story_shell_view.NewRequest(), nullptr);

  preloaded_story_shell_ =
      std::make_unique<StoryShellConnection>(StoryShellConnection{
          std::move(story_shell_controller), std::move(story_shell_services),
          std::move(story_shell_view)});
}

void StoryProviderImpl::SetStoryInfoExtra(const fidl::String& story_id,
                                          const fidl::String& name,
                                          const fidl::String& value,
                                          const std::function<void()>& done) {
  auto mutate = [name, value](StoryData* const story_data) {
    story_data->story_info->extra[name] = value;
    return true;
  };

  new MutateStoryDataCall(&operation_queue_, page(), story_id, mutate,
                          done);
};

// |StoryProvider|
void StoryProviderImpl::CreateStory(const fidl::String& module_url,
                                    const CreateStoryCallback& callback) {
  FTL_LOG(INFO) << "CreateStory() " << module_url;
  new CreateStoryCall(&operation_queue_, ledger_client_->ledger(), page(), this, module_url,
                      FidlStringMap(), fidl::String(), callback);
}

// |StoryProvider|
void StoryProviderImpl::CreateStoryWithInfo(
    const fidl::String& module_url,
    FidlStringMap extra_info,
    const fidl::String& root_json,
    const CreateStoryWithInfoCallback& callback) {
  FTL_LOG(INFO) << "CreateStoryWithInfo() " << root_json;
  new CreateStoryCall(&operation_queue_, ledger_client_->ledger(), page(), this, module_url,
                      std::move(extra_info), root_json, callback);
}

// |StoryProvider|
void StoryProviderImpl::DeleteStory(const fidl::String& story_id,
                                    const DeleteStoryCallback& callback) {
  new DeleteStoryCall(&operation_queue_, page(), story_id,
                      &story_controller_impls_,
                      component_context_info_.message_queue_manager,
                      false /* already_deleted */, callback);
}

// |StoryProvider|
void StoryProviderImpl::GetStoryInfo(const fidl::String& story_id,
                                     const GetStoryInfoCallback& callback) {
  MakeGetStoryDataCall(
      &operation_queue_, page(), story_id,
      [callback](StoryDataPtr story_data) {
        callback(story_data ? std::move(story_data->story_info) : nullptr);
      });
}

// Called by StoryControllerImpl on behalf of ModuleContextImpl
void StoryProviderImpl::RequestStoryFocus(const fidl::String& story_id) {
  FTL_LOG(INFO) << "RequestStoryFocus() " << story_id;
  focus_provider_->Request(story_id);
}

void StoryProviderImpl::NotifyStoryStateChange(const fidl::String& story_id,
                                               const StoryState story_state) {
  auto i = story_controller_impls_.find(story_id);

  if (i == story_controller_impls_.end()) {
    // If this call arrives while DeleteStory() is in progress, the story
    // controller might already be gone from here.
    return;
  }

  const StoryInfo* const story_info = i->second.current_info.get();
  NotifyStoryWatchers(story_info, story_state);
}

// |StoryProvider|
void StoryProviderImpl::GetController(
    const fidl::String& story_id,
    fidl::InterfaceRequest<StoryController> request) {
  new GetControllerCall(&operation_queue_, page(), this,
                        &story_controller_impls_, story_id, std::move(request));
}

// |StoryProvider|
void StoryProviderImpl::PreviousStories(
    const PreviousStoriesCallback& callback) {
  new ReadAllDataCall<StoryData>(
      &operation_queue_, page(), kStoryKeyPrefix, XdrStoryData,
      [callback](fidl::Array<StoryDataPtr> data) {
        fidl::Array<fidl::String> result;
        result.resize(0);

        for (auto& story_data : data) {
          result.push_back(story_data->story_info->id);
        }

        callback(std::move(result));
      });
}

// |StoryProvider|
void StoryProviderImpl::RunningStories(const RunningStoriesCallback& callback) {
  auto stories = fidl::Array<fidl::String>::New(0);
  for (const auto& impl_container : story_controller_impls_) {
    if (impl_container.second.impl->IsRunning()) {
      stories.push_back(impl_container.second.impl->GetStoryId());
    }
  }
  callback(std::move(stories));
}

// |StoryProvider|
void StoryProviderImpl::GetImportance(const GetImportanceCallback& callback) {
  // This is an Operation on the queue mostly so a story controller cannot be
  // deleted while we wait for it to compute its importance.
  //
  // TODO(mesch): Should be cached or precomputed really. For now we happily use
  // the opportunity to put some load on the ledger, so gather performance
  // metrics.
  new GetImportanceCall(&operation_queue_, this, callback);
}

// |StoryProvider|
void StoryProviderImpl::WatchImportance(
    fidl::InterfaceHandle<StoryImportanceWatcher> watcher) {
  importance_watchers_.AddInterfacePtr(
      StoryImportanceWatcherPtr::Create(std::move(watcher)));
}

// |PageClient|
void StoryProviderImpl::OnPageChange(const std::string& /*key*/,
                                     const std::string& value) {
  auto story_data = StoryData::New();
  if (!XdrRead(value, &story_data, XdrStoryData)) {
    return;
  }

  // HACK(jimbe) We don't have the page and it's expensive to get it, so just
  // mark it as STOPPED. We know it's not running or we'd have a
  // StoryController.
  //
  // If we have a StoryControllerImpl for this story id, update our cached
  // StoryInfo.
  StoryState state = StoryState::STOPPED;
  auto i = story_controller_impls_.find(story_data->story_info->id);
  if (i != story_controller_impls_.end()) {
    state = i->second.impl->GetStoryState();
    i->second.current_info = story_data->story_info.Clone();
  }

  NotifyStoryWatchers(story_data->story_info.get(), state);
}

// |PageClient|
void StoryProviderImpl::OnPageDelete(const std::string& key) {
  // Extract the story ID from the ledger key. cf. kStoryKeyPrefix.
  const fidl::String story_id = key.substr(sizeof(kStoryKeyPrefix) - 1);

  watchers_.ForAllPtrs([&story_id](StoryProviderWatcher* const watcher) {
    watcher->OnDelete(story_id);
  });

  new DeleteStoryCall(&operation_queue_, page(), story_id,
                      &story_controller_impls_,
                      component_context_info_.message_queue_manager,
                      true /* already_deleted */, [] {});
}

// |FocusWatcher|
void StoryProviderImpl::OnFocusChange(FocusInfoPtr info) {
  if (info->device_id.get() != device_id_) {
    return;
  }

  if (info->focused_story_id.is_null()) {
    return;
  }

  auto i = story_controller_impls_.find(info->focused_story_id.get());
  if (i == story_controller_impls_.end()) {
    FTL_LOG(ERROR) << "Story controller not found for focused story "
                   << info->focused_story_id;
    return;
  }

  // Focusing changes importance, but the log needs to be written first.
  i->second.impl->Log(MakeLogEntry(StorySignal::FOCUSED));
  i->second.impl->Sync([this] { NotifyImportanceWatchers(); });

  // Last focus time is recorded in the ledger, and story provider watchers are
  // notified through the page watcher.
  auto mutate = [time =
                     mx_time_get(MX_CLOCK_UTC)](StoryData* const story_data) {
    story_data->story_info->last_focus_time = time;
    return true;
  };
  new MutateStoryDataCall(&operation_queue_, page(), info->focused_story_id,
                          mutate, [] {});
}

void StoryProviderImpl::OnContextChange() {
  NotifyImportanceWatchers();
}

void StoryProviderImpl::NotifyImportanceWatchers() {
  // TODO(mesch): This notification may be triggered because context changes,
  // which can change importance of all stories, or because single story
  // changed, which would require to compute importance only of the single
  // story. But here we cannot distinguish, and will always recompute
  // everything.
  importance_watchers_.ForAllPtrs(
      [this](StoryImportanceWatcher* const watcher) {
        watcher->OnImportanceChange();
      });
}

void StoryProviderImpl::NotifyStoryWatchers(const StoryInfo* const story_info,
                                            const StoryState story_state) {
  watchers_.ForAllPtrs(
      [story_info, story_state](StoryProviderWatcher* const watcher) {
        watcher->OnChange(story_info->Clone(), story_state);
      });
}

StoryContextLogPtr StoryProviderImpl::MakeLogEntry(const StorySignal signal) {
  auto log_entry = StoryContextLog::New();
  log_entry->context = context_handler_.values().Clone();
  log_entry->device_id = device_id_;
  log_entry->time = mx_time_get(MX_CLOCK_UTC);
  log_entry->signal = signal;

  return log_entry;
}

void StoryProviderImpl::GetLinkPeer(const fidl::String& story_id,
                                    fidl::Array<fidl::String> module_path,
                                    const fidl::String& link_name,
                                    fidl::InterfaceRequest<Link> request) {
  new GetLinkPeerCall(&operation_queue_, this, story_id, std::move(module_path),
                      link_name, std::move(request));
}

}  // namespace modular
