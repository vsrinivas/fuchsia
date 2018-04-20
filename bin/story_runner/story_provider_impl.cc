// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/story_runner/story_provider_impl.h"

#include <memory>
#include <utility>
#include <vector>

#include <fuchsia/cpp/modular.h>
#include <fuchsia/cpp/modular_private.h>
#include <fuchsia/cpp/views_v1.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/zx/time.h>

#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/vmo/strings.h"
#include "peridot/bin/device_runner/cobalt/cobalt.h"
#include "peridot/bin/story_runner/link_impl.h"
#include "peridot/bin/story_runner/story_controller_impl.h"
#include "peridot/bin/user_runner/focus.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/fidl/clone.h"
#include "peridot/lib/fidl/json_xdr.h"
#include "peridot/lib/fidl/proxy.h"
#include "peridot/lib/ledger_client/operations.h"
#include "peridot/lib/ledger_client/page_id.h"
#include "peridot/lib/ledger_client/storage.h"
#include "peridot/lib/rapidjson/rapidjson.h"

namespace modular {

namespace {

// Serialization and deserialization of modular_private::StoryData and StoryInfo
// to and from JSON.

void XdrStoryInfoExtraEntry(XdrContext* const xdr,
                            StoryInfoExtraEntry* const data) {
  xdr->Field("key", &data->key);
  xdr->Field("value", &data->value);
}

void XdrStoryInfo(XdrContext* const xdr, StoryInfo* const data) {
  xdr->Field("last_focus_time", &data->last_focus_time);
  xdr->Field("url", &data->url);
  xdr->Field("id", &data->id);
  xdr->Field("extra", &data->extra, XdrStoryInfoExtraEntry);
}

void XdrStoryData(XdrContext* const xdr,
                  modular_private::StoryData* const data) {
  static constexpr char kStoryPageId[] = "story_page_id";
  xdr->Field("story_info", &data->story_info, XdrStoryInfo);
  switch (xdr->op()) {
    case XdrOp::FROM_JSON: {
      std::string page_id;
      xdr->Field(kStoryPageId, &page_id);
      if (page_id.empty()) {
        data->story_page_id = nullptr;
      } else {
        data->story_page_id = ledger::PageId::New();
        *data->story_page_id = PageIdFromBase64(page_id);
      }
      break;
    }
    case XdrOp::TO_JSON: {
      std::string page_id;
      if (data->story_page_id) {
        page_id = PageIdToBase64(*data->story_page_id);
      }
      xdr->Field(kStoryPageId, &page_id);
      break;
    }
  }
}

void MakeGetStoryDataCall(
    OperationContainer* const container,
    ledger::Page* const page,
    fidl::StringPtr story_id,
    std::function<void(modular_private::StoryDataPtr)> result_call) {
  new ReadDataCall<modular_private::StoryData>(
      container, page, MakeStoryKey(story_id), true /* not_found_is_ok */,
      XdrStoryData, std::move(result_call));
};

void MakeWriteStoryDataCall(OperationContainer* const container,
                            ledger::Page* const page,
                            modular_private::StoryDataPtr story_data,
                            std::function<void()> result_call) {
  new WriteDataCall<modular_private::StoryData>(
      container, page, MakeStoryKey(story_data->story_info.id), XdrStoryData,
      std::move(story_data), std::move(result_call));
};

}  // namespace

class StoryProviderImpl::MutateStoryDataCall : Operation<> {
 public:
  MutateStoryDataCall(
      OperationContainer* const container,
      ledger::Page* const page,
      fidl::StringPtr story_id,
      std::function<bool(modular_private::StoryData* story_data)> mutate,
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

    MakeGetStoryDataCall(
        &operation_queue_, page_, story_id_,
        [this, flow](modular_private::StoryDataPtr story_data) {
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
                                 std::move(story_data), [flow] {});
        });
  }

  ledger::Page* const page_;  // not owned
  const fidl::StringPtr story_id_;
  std::function<bool(modular_private::StoryData* story_data)> mutate_;

  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MutateStoryDataCall);
};

// 1. Create a page for the new story.
// 2. Create a new modular_private::StoryData structure pointing to this new
// page and save it
//    to the root page.
// 3. Write a copy of the current context to the story page.
// 4. Returns the Story ID of the newly created story.
class StoryProviderImpl::CreateStoryCall : Operation<fidl::StringPtr> {
 public:
  CreateStoryCall(OperationContainer* const container,
                  ledger::Ledger* const ledger,
                  ledger::Page* const root_page,
                  StoryProviderImpl* const story_provider_impl,
                  fidl::StringPtr url,
                  fidl::VectorPtr<StoryInfoExtraEntry> extra_info,
                  fidl::StringPtr root_json,
                  ResultCall result_call)
      : Operation("StoryProviderImpl::CreateStoryCall",
                  container,
                  std::move(result_call)),
        ledger_(ledger),
        root_page_(root_page),
        story_provider_impl_(story_provider_impl),
        url_(url),
        extra_info_(std::move(extra_info)),
        root_json_(root_json),
        start_time_(zx_clock_get(ZX_CLOCK_UTC)) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &story_id_};

    ledger_->GetPage(
        nullptr, story_page_.NewRequest(), [this, flow](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FXL_LOG(ERROR) << trace_name() << " "
                           << "Ledger.GetPage() " << status;
            return;
          }

          story_page_->GetId([this, flow](ledger::PageId id) {
            story_page_id_ = std::move(id);

            // TODO(security), cf. FW-174. This ID is exposed in
            // public services such as
            // StoryProvider.PreviousStories(),
            // StoryController.GetInfo(),
            // ModuleContext.GetStoryId(). We need to ensure this
            // doesn't expose internal information by being a
            // page ID.
            story_id_ = to_hex_string(story_page_id_.id);

            story_data_ = modular_private::StoryData::New();
            story_data_->story_page_id = CloneOptional(story_page_id_);
            story_data_->story_info.url = url_;
            story_data_->story_info.id = story_id_;
            story_data_->story_info.last_focus_time =
                zx_clock_get(ZX_CLOCK_UTC);
            story_data_->story_info.extra = std::move(extra_info_);

            MakeWriteStoryDataCall(&operation_queue_, root_page_,
                                   std::move(story_data_),
                                   [this, flow] { Cont1(flow); });
          });
        });
  }

  void Cont1(FlowToken flow) {
    controller_ = std::make_unique<StoryControllerImpl>(
        story_id_, story_provider_impl_->ledger_client_, story_page_id_,
        story_provider_impl_);
    auto create_link_info = CreateLinkInfo::New();
    create_link_info->initial_data = std::move(root_json_);

    controller_->AddForCreate(kRootModuleName, url_, kRootLink,
                              std::move(create_link_info),
                              [this, flow] { Cont2(flow); });
  }

  void Cont2(FlowToken flow) {
    // We ensure that everything has been written to the story page before this
    // operation is done.
    controller_->Sync([flow] {});

    ReportStoryLaunchTime(zx_clock_get(ZX_CLOCK_UTC) - start_time_);
  }

  ledger::Ledger* const ledger_;                  // not owned
  ledger::Page* const root_page_;                 // not owned
  StoryProviderImpl* const story_provider_impl_;  // not owned
  const fidl::StringPtr module_name_;
  const fidl::StringPtr url_;
  fidl::VectorPtr<StoryInfoExtraEntry> extra_info_;
  fidl::StringPtr root_json_;
  const zx_time_t start_time_;

  ledger::PagePtr story_page_;
  modular_private::StoryDataPtr story_data_;
  std::unique_ptr<StoryControllerImpl> controller_;

  ledger::PageId story_page_id_;
  fidl::StringPtr story_id_;  // This is the result of the Operation.

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CreateStoryCall);
};

class StoryProviderImpl::DeleteStoryCall : Operation<> {
 public:
  using StoryControllerImplMap =
      std::map<std::string, struct StoryControllerImplContainer>;
  using PendingDeletion = std::pair<std::string, DeleteStoryCall*>;

  DeleteStoryCall(OperationContainer* const container,
                  ledger::Page* const page,
                  fidl::StringPtr story_id,
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
                        FXL_LOG(ERROR) << trace_name() << " "
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

    FXL_DCHECK(i->second.impl != nullptr);
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
    async::PostTask(async_get_default(), [this, flow] {
      story_controller_impls_->erase(story_id_);
      message_queue_manager_->DeleteNamespace(
          EncodeModuleComponentNamespace(story_id_), [flow] {});

      // TODO(mesch): We must delete the story page too.
    });
  }

 private:
  ledger::Page* const page_;  // not owned
  const fidl::StringPtr story_id_;
  StoryControllerImplMap* const story_controller_impls_;
  MessageQueueManager* const message_queue_manager_;
  const bool already_deleted_;  // True if called from OnChange();

  FXL_DISALLOW_COPY_AND_ASSIGN(DeleteStoryCall);
};

// 1. Ensure that the story data in the root page isn't dirty due to a crash
// 2. Retrieve the page specific to this story.
// 3. Return a controller for this story that contains the page pointer.
class StoryProviderImpl::GetControllerCall : Operation<> {
 public:
  using StoryControllerImplMap =
      std::map<std::string, struct StoryControllerImplContainer>;

  GetControllerCall(OperationContainer* const container,
                    ledger::Page* const page,
                    StoryProviderImpl* const story_provider_impl,
                    StoryControllerImplMap* const story_controller_impls,
                    fidl::StringPtr story_id,
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
    // This won't race against itself because it's managed by an operation
    // queue.
    auto i = story_controller_impls_->find(story_id_);
    if (i != story_controller_impls_->end()) {
      i->second.impl->Connect(std::move(request_));
      return;
    }

    MakeGetStoryDataCall(
        &operation_queue_, page_, story_id_,
        [this, flow](modular_private::StoryDataPtr story_data) {
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
        *story_data_->story_page_id, story_provider_impl_);
    container.impl->Connect(std::move(request_));
    container.current_info = CloneOptional(story_data_->story_info);
    story_controller_impls_->emplace(story_id_, std::move(container));
  }

  ledger::Page* const page_;                      // not owned
  StoryProviderImpl* const story_provider_impl_;  // not owned
  StoryControllerImplMap* const story_controller_impls_;
  const fidl::StringPtr story_id_;
  fidl::InterfaceRequest<StoryController> request_;

  modular_private::StoryDataPtr story_data_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetControllerCall);
};

class StoryProviderImpl::StopAllStoriesCall : Operation<> {
 public:
  StopAllStoriesCall(OperationContainer* const container,
                     StoryProviderImpl* const story_provider_impl,
                     ResultCall result_call)
      : Operation("StoryProviderImpl::StopAllStoriesCall",
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
      it.second.impl->StopForTeardown([this, story_id = it.first, flow] {
        // It is okay to erase story_id because story provider binding has been
        // closed and this callback cannot be invoked synchronously.
        story_provider_impl_->story_controller_impls_.erase(story_id);
      });
    }
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(StopAllStoriesCall);
};

class StoryProviderImpl::StopStoryShellCall : Operation<> {
 public:
  StopStoryShellCall(OperationContainer* const container,
                     StoryProviderImpl* const story_provider_impl,
                     ResultCall result_call)
      : Operation("StoryProviderImpl::StopStoryShellCall",
                  container,
                  std::move(result_call)),
        story_provider_impl_(story_provider_impl) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this};
    if (story_provider_impl_->preloaded_story_shell_) {
      // Calling Teardown() below will branch |flow| into normal and timeout
      // paths. |flow| must go out of scope when either of the paths finishes.
      FlowTokenHolder branch{flow};
      story_provider_impl_->preloaded_story_shell_->story_shell_app->Teardown(
          kBasicTimeout,
          [branch] { std::unique_ptr<FlowToken> flow = branch.Continue(); });
    }
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned

  FXL_DISALLOW_COPY_AND_ASSIGN(StopStoryShellCall);
};

struct StoryProviderImpl::LinkPeer {
  std::unique_ptr<LedgerClient> ledger;
  std::unique_ptr<LinkImpl> link;
};

class StoryProviderImpl::GetLinkPeerCall : Operation<> {
 public:
  GetLinkPeerCall(OperationContainer* const container,
                  StoryProviderImpl* const impl,
                  fidl::StringPtr story_id,
                  fidl::VectorPtr<fidl::StringPtr> module_path,
                  fidl::StringPtr link_name,
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

    MakeGetStoryDataCall(
        &operation_queue_, impl_->page(), story_id_,
        [this, flow](modular_private::StoryDataPtr story_data) {
          if (story_data) {
            story_data_ = std::move(story_data);
            Cont(flow);
          }
        });
  }

  void Cont(FlowToken flow) {
    auto link_peer = std::make_unique<LinkPeer>();

    link_peer->ledger = impl_->ledger_client_->GetLedgerClientPeer();

    auto link_path = LinkPath::New();
    link_path->module_path = module_path_.Clone();
    link_path->link_name = link_name_;

    link_peer->link = std::make_unique<LinkImpl>(
        link_peer->ledger.get(), CloneStruct(*story_data_->story_page_id),
        std::move(*link_path), nullptr);

    link_peer->link->Connect(std::move(request_),
                             LinkImpl::ConnectionType::Primary);

    impl_->link_peers_.emplace_back(std::move(link_peer));

    // TODO(mesch): Set an orphaned handler so that link peers get dropped
    // earlier than at logout.
  }

  StoryProviderImpl* const impl_;  // not owned
  const fidl::StringPtr story_id_;
  const fidl::VectorPtr<fidl::StringPtr> module_path_;
  const fidl::StringPtr link_name_;
  fidl::InterfaceRequest<Link> request_;

  modular_private::StoryDataPtr story_data_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GetLinkPeerCall);
};

class StoryProviderImpl::DumpStateCall : Operation<std::string> {
 public:
  DumpStateCall(OperationContainer* const container,
                StoryProviderImpl* const story_provider_impl,
                ResultCall result_call)
      : Operation("StoryProviderImpl::CreateStoryCall",
                  container,
                  std::move(result_call)),
        story_provider_impl_(story_provider_impl) {
    Ready();
  }

 private:
  void Run() override {
    FlowToken flow{this, &dump_};

    output_ << "=================Begin story provider info=======" << std::endl;

    new DumpPageSnapshotCall(&operation_queue_, story_provider_impl_->page(),
                             [this, flow](auto dump) { output_ << dump; });
    new ReadAllDataCall<modular_private::StoryData>(
        &operation_queue_, story_provider_impl_->page(), kStoryKeyPrefix,
        XdrStoryData,
        [this, flow](fidl::VectorPtr<modular_private::StoryData> data) {
          for (size_t i = 0; i < data->size(); ++i) {
            DumpStoryPage(std::move(data->at(i)), flow);
          }

          // This needs to be the last operations on |operation_queue_| since we
          // need to get all the content from |output_| into |dump_|.
          new SyncCall(&operation_queue_,
                       [this, flow] { dump_ = output_.str(); });
        });
  }

  void DumpStoryPage(modular_private::StoryData story_data, FlowToken flow) {
    auto story_id = std::move(story_data.story_info.id);
    auto page_id = std::move(story_data.story_page_id);
    ledger::PagePtr story_page;
    story_provider_impl_->ledger_client_->ledger()->GetPage(
        std::move(page_id), story_page.NewRequest(), [](auto s) {});
    story_pages_.push_back(std::move(story_page));
    new DumpPageSnapshotCall(&operation_queue_, story_pages_.back().get(),
                             [this, story_id, flow](auto dump) {
                               output_
                                   << "=================Story id: " << story_id
                                   << "===========" << std::endl;
                               output_ << dump;
                             });
  }

  StoryProviderImpl* const story_provider_impl_;  // not owned

  std::vector<ledger::PagePtr> story_pages_;

  std::string dump_;
  std::ostringstream output_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DumpStateCall);
};

StoryProviderImpl::StoryProviderImpl(
    Scope* const user_scope,
    std::string device_id,
    LedgerClient* const ledger_client,
    LedgerPageId root_page_id,
    AppConfig story_shell,
    const ComponentContextInfo& component_context_info,
    FocusProviderPtr focus_provider,
    UserIntelligenceProvider* const user_intelligence_provider,
    ModuleResolver* module_resolver,
    const bool test)
    : PageClient("StoryProviderImpl",
                 ledger_client,
                 std::move(root_page_id),
                 kStoryKeyPrefix),
      user_scope_(user_scope),
      device_id_(std::move(device_id)),
      ledger_client_(ledger_client),
      story_shell_(std::move(story_shell)),
      test_(test),
      component_context_info_(component_context_info),
      user_intelligence_provider_(user_intelligence_provider),
      module_resolver_(module_resolver),
      focus_provider_(std::move(focus_provider)),
      focus_watcher_binding_(this),
      weak_factory_(this) {
  focus_provider_->Watch(focus_watcher_binding_.NewBinding());
  if (!test_) {
    // As an optimization, since app startup time is long, we optimistically
    // load a story shell instance even if there are no stories that need it
    // yet. This can reduce the time to first frame.
    MaybeLoadStoryShellDelayed();
  }
}

StoryProviderImpl::~StoryProviderImpl() = default;

void StoryProviderImpl::Connect(fidl::InterfaceRequest<StoryProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void StoryProviderImpl::StopAllStories(const std::function<void()>& callback) {
  new StopAllStoriesCall(&operation_queue_, this, callback);
}

void StoryProviderImpl::Teardown(const std::function<void()>& callback) {
  // Closing all binding to this instance ensures that no new messages come
  // in, though previous messages need to be processed. The stopping of stories
  // is done on |operation_queue_| since that must strictly happen after all
  // pending messgages have been processed.
  bindings_.CloseAll();
  new StopAllStoriesCall(&operation_queue_, this, [] {});
  new StopStoryShellCall(&operation_queue_, this, callback);
}

// |StoryProvider|
void StoryProviderImpl::Watch(
    fidl::InterfaceHandle<StoryProviderWatcher> watcher) {
  auto watcher_ptr = watcher.Bind();
  for (const auto& item : story_controller_impls_) {
    const auto& container = item.second;
    watcher_ptr->OnChange(CloneStruct(*container.current_info),
                          container.impl->GetStoryState());
  }
  watchers_.AddInterfacePtr(std::move(watcher_ptr));
}

// |StoryProvider|
void StoryProviderImpl::Duplicate(
    fidl::InterfaceRequest<StoryProvider> request) {
  Connect(std::move(request));
}

std::unique_ptr<AppClient<Lifecycle>> StoryProviderImpl::StartStoryShell(
    fidl::InterfaceRequest<views_v1_token::ViewOwner> request) {
  MaybeLoadStoryShell();

  auto preloaded_story_shell = std::move(preloaded_story_shell_);
  auto app_client = std::move(preloaded_story_shell->story_shell_app);

  proxies_.Connect(std::move(preloaded_story_shell->story_shell_view),
                   std::move(request));

  // Kickoff another StoryShell, to make it faster for next story. We optimize
  // even further by delaying the loading of the next story shell instance by
  // waiting a few seconds.
  if (!test_) {
    MaybeLoadStoryShellDelayed();
  }

  return app_client;
}

void StoryProviderImpl::MaybeLoadStoryShellDelayed() {
  async::PostDelayedTask(async_get_default(),
                         [weak_this = weak_factory_.GetWeakPtr()] {
                           if (weak_this) {
                             new SyncCall(&weak_this->operation_queue_,
                                          [weak_this] {
                                            if (weak_this) {
                                              weak_this->MaybeLoadStoryShell();
                                            }
                                          });
                           }
                         },
                         zx::sec(5));
}

void StoryProviderImpl::MaybeLoadStoryShell() {
  if (preloaded_story_shell_) {
    return;
  }

  auto story_shell_app = std::make_unique<AppClient<Lifecycle>>(
      user_scope_->GetLauncher(), CloneStruct(story_shell_));

  // CreateView must be called in order to get the Flutter application to run

  views_v1::ViewProviderPtr view_provider;
  story_shell_app->services().ConnectToService(view_provider.NewRequest());

  views_v1_token::ViewOwnerPtr story_shell_view;
  view_provider->CreateView(story_shell_view.NewRequest(), nullptr);

  preloaded_story_shell_ =
      std::make_unique<StoryShellConnection>(StoryShellConnection{
          std::move(story_shell_app), std::move(story_shell_view)});
}

void StoryProviderImpl::SetStoryInfoExtra(fidl::StringPtr story_id,
                                          fidl::StringPtr name,
                                          fidl::StringPtr value,
                                          const std::function<void()>& done) {
  auto mutate = [name, value](modular_private::StoryData* const story_data) {
    StoryInfoExtraEntry entry;
    entry.key = name;
    entry.value = value;
    story_data->story_info.extra.push_back(std::move(entry));
    return true;
  };

  new MutateStoryDataCall(&operation_queue_, page(), story_id, mutate, done);
};

void StoryProviderImpl::DumpState(
    const std::function<void(const std::string&)>& callback) {
  new DumpStateCall(&operation_queue_, this,
                    [callback](auto dump) { callback(dump); });
}

// |StoryProvider|
void StoryProviderImpl::CreateStory(fidl::StringPtr module_url,
                                    CreateStoryCallback callback) {
  FXL_LOG(INFO) << "CreateStory() " << module_url;
  new CreateStoryCall(&operation_queue_, ledger_client_->ledger(), page(), this,
                      module_url, nullptr, fidl::StringPtr(), callback);
}

// |StoryProvider|
void StoryProviderImpl::CreateStoryWithInfo(
    fidl::StringPtr module_url,
    fidl::VectorPtr<StoryInfoExtraEntry> extra_info,
    fidl::StringPtr root_json,
    CreateStoryWithInfoCallback callback) {
  FXL_LOG(INFO) << "CreateStoryWithInfo() " << module_url << " " << root_json;
  new CreateStoryCall(&operation_queue_, ledger_client_->ledger(), page(), this,
                      module_url, std::move(extra_info), root_json, callback);
}

// |StoryProvider|
void StoryProviderImpl::DeleteStory(fidl::StringPtr story_id,
                                    DeleteStoryCallback callback) {
  new DeleteStoryCall(&operation_queue_, page(), story_id,
                      &story_controller_impls_,
                      component_context_info_.message_queue_manager,
                      false /* already_deleted */, callback);
}

// |StoryProvider|
void StoryProviderImpl::GetStoryInfo(fidl::StringPtr story_id,
                                     GetStoryInfoCallback callback) {
  MakeGetStoryDataCall(
      &operation_queue_, page(), story_id,
      [callback](modular_private::StoryDataPtr story_data) {
        callback(story_data
                     ? fidl::MakeOptional(std::move(story_data->story_info))
                     : nullptr);
      });
}

// Called by StoryControllerImpl on behalf of ModuleContextImpl
void StoryProviderImpl::RequestStoryFocus(fidl::StringPtr story_id) {
  FXL_LOG(INFO) << "RequestStoryFocus() " << story_id;
  focus_provider_->Request(story_id);
}

void StoryProviderImpl::NotifyStoryStateChange(fidl::StringPtr story_id,
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
    fidl::StringPtr story_id,
    fidl::InterfaceRequest<StoryController> request) {
  new GetControllerCall(&operation_queue_, page(), this,
                        &story_controller_impls_, story_id, std::move(request));
}

// |StoryProvider|
void StoryProviderImpl::PreviousStories(PreviousStoriesCallback callback) {
  new ReadAllDataCall<modular_private::StoryData>(
      &operation_queue_, page(), kStoryKeyPrefix, XdrStoryData,
      [callback](fidl::VectorPtr<modular_private::StoryData> data) {
        auto result = fidl::VectorPtr<modular::StoryInfo>::New(0);

        for (auto& story_data : *data) {
          result.push_back(std::move(story_data.story_info));
        }

        callback(std::move(result));
      });
}

// |StoryProvider|
void StoryProviderImpl::RunningStories(RunningStoriesCallback callback) {
  auto stories = fidl::VectorPtr<fidl::StringPtr>::New(0);
  for (const auto& impl_container : story_controller_impls_) {
    if (impl_container.second.impl->IsRunning()) {
      stories.push_back(impl_container.second.impl->GetStoryId());
    }
  }
  callback(std::move(stories));
}

// |PageClient|
void StoryProviderImpl::OnPageChange(const std::string& /*key*/,
                                     const std::string& value) {
  auto story_data = modular_private::StoryData::New();
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
  auto i = story_controller_impls_.find(story_data->story_info.id);
  if (i != story_controller_impls_.end()) {
    state = i->second.impl->GetStoryState();
    i->second.current_info = CloneOptional(story_data->story_info);
  }

  NotifyStoryWatchers(&story_data->story_info, state);
}

// |PageClient|
void StoryProviderImpl::OnPageDelete(const std::string& key) {
  // Extract the story ID from the ledger key. cf. kStoryKeyPrefix.
  const fidl::StringPtr story_id = key.substr(sizeof(kStoryKeyPrefix) - 1);

  for (const auto& i : watchers_.ptrs()) {
    (*i)->OnDelete(story_id);
  }

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
    FXL_LOG(ERROR) << "Story controller not found for focused story "
                   << info->focused_story_id;
    return;
  }

  // Last focus time is recorded in the ledger, and story provider watchers are
  // notified through the page watcher.
  auto mutate = [time = zx_clock_get(ZX_CLOCK_UTC)](
                    modular_private::StoryData* const story_data) {
    story_data->story_info.last_focus_time = time;
    return true;
  };
  new MutateStoryDataCall(&operation_queue_, page(), info->focused_story_id,
                          mutate, [] {});
}

void StoryProviderImpl::NotifyStoryWatchers(const StoryInfo* const story_info,
                                            const StoryState story_state) {
  for (const auto& i : watchers_.ptrs()) {
    (*i)->OnChange(CloneStruct(*story_info), story_state);
  }
}

void StoryProviderImpl::GetLinkPeer(
    fidl::StringPtr story_id,
    fidl::VectorPtr<fidl::StringPtr> module_path,
    fidl::StringPtr link_name,
    fidl::InterfaceRequest<Link> request) {
  new GetLinkPeerCall(&operation_queue_, this, story_id, std::move(module_path),
                      link_name, std::move(request));
}

}  // namespace modular
