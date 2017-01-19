// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_provider_impl.h"

#include <stdlib.h>
#include <time.h>
#include <unordered_set>

#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/services/story/resolver.fidl.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/functional/make_copyable.h"

namespace modular {
namespace {

void InitStoryId() {
  // If rand() is not seeded, it always returns the same sequence of numbers.
  srand(time(nullptr));
}

// Generates a unique randomly generated string of |length| size to be
// used as a story id.
std::string MakeStoryId(std::unordered_set<std::string>* story_ids,
                        const size_t length) {
  std::function<char()> randchar = [] {
    const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    const size_t max_index = (sizeof(charset) - 1);
    return charset[rand() % max_index];
  };

  std::string id(length, 0);
  std::generate_n(id.begin(), length, randchar);

  if (story_ids->find(id) != story_ids->end()) {
    return MakeStoryId(story_ids, length);
  }

  story_ids->insert(id);
  return id;
}

// Below are helper classes that encapsulates a chain of asynchronous
// operations on the Ledger. Because the operations all return
// something, the handles on which they are invoked need to be kept
// around until the return value arrives. This precludes them to be
// local variables. The next thing that comes to mind is to make them
// fields of the containing object. However, there might be multiple
// such operations going on concurrently in one StoryProviderImpl, so
// they cannot be fields of StoryProviderImpl. Thus such operations
// are separate classes for now, until I can think of something
// better, or we change the API to interface requests.
//
// NOTE(mesch): After these classes were written, the API was changed
// to InterfaceRequests. Most of the nesting can be removed now,
// unless we want to check status, which is still returned. Status
// checking was useful in debugging ledger, so I let the nesting in
// place for now.

class GetStoryDataCall : public Operation {
 public:
  using Result = std::function<void(StoryDataPtr)>;

  GetStoryDataCall(OperationContainer* const container,
                   ledger::Ledger* const ledger,
                   const fidl::String& story_id,
                   Result result)
      : Operation(container),
        ledger_(ledger),
        story_id_(story_id),
        result_(result) {
    Ready();
  }

  void Run() override {
    ledger_->GetRootPage(
        root_page_.NewRequest(), [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "GetStoryDataCall() " << story_id_
                           << " Ledger.GetRootPage() " << status;
            result_(std::move(story_data_));
            Done();
            return;
          }

          root_page_->GetSnapshot(
              root_snapshot_.NewRequest(), nullptr, [this](ledger::Status status) {
                if (status != ledger::Status::OK) {
                  FTL_LOG(ERROR) << "GetStoryDataCall() " << story_id_
                                 << " Page.GetSnapshot() " << status;
                  result_(std::move(story_data_));
                  Done();
                  return;
                }

                root_snapshot_->Get(
                    to_array(story_id_),
                    [this](ledger::Status status, ledger::ValuePtr value) {
                      if (status != ledger::Status::OK) {
                        FTL_LOG(ERROR) << "GetStoryDataCall() " << story_id_
                                       << " PageSnapshot.Get() " << status;
                        result_(std::move(story_data_));
                        Done();
                        return;
                      }

                      story_data_ = StoryData::New();
                      story_data_->Deserialize(value->get_bytes().data(),
                                               value->get_bytes().size());

                      result_(std::move(story_data_));
                      Done();
                    });
              });
        });
  };

 private:
  ledger::Ledger* const ledger_;  // not owned
  const fidl::String story_id_;
  Result result_;

  ledger::PagePtr root_page_;
  ledger::PageSnapshotPtr root_snapshot_;
  StoryDataPtr story_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetStoryDataCall);
};

class WriteStoryDataCall : public Operation {
 public:
  using Result = std::function<void()>;

  WriteStoryDataCall(OperationContainer* const container,
                     ledger::Ledger* const ledger,
                     StoryDataPtr story_data,
                     Result result)
      : Operation(container),
        ledger_(ledger),
        story_data_(std::move(story_data)),
        result_(result) {
    Ready();
  }

  void Run() override {
    FTL_DCHECK(!story_data_.is_null());

    ledger_->GetRootPage(
        root_page_.NewRequest(), [this](ledger::Status status) {
          const fidl::String& story_id = story_data_->story_info->id;
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "WriteStoryDataCall() " << story_id
                           << " Ledger.GetRootPage() " << status;
            result_();
            Done();
            return;
          }

          const size_t size = story_data_->GetSerializedSize();
          fidl::Array<uint8_t> value = fidl::Array<uint8_t>::New(size);
          story_data_->Serialize(value.data(), size);

          root_page_->PutWithPriority(
              to_array(story_id), std::move(value), ledger::Priority::EAGER,
              [this](ledger::Status status) {
                if (status != ledger::Status::OK) {
                  const fidl::String& story_id = story_data_->story_info->id;
                  FTL_LOG(ERROR) << "WriteStoryDataCall() " << story_id
                                 << " Page.PutWithPriority() " << status;
                }

                result_();
                Done();
              });
        });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  StoryDataPtr story_data_;
  ledger::PagePtr root_page_;
  Result result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteStoryDataCall);
};

class CreateStoryCall : public Operation {
 public:
  using FidlStringMap = StoryProviderImpl::FidlStringMap;
  using Result = std::function<void(fidl::String)>;

  CreateStoryCall(OperationContainer* const container,
                  ledger::Ledger* const ledger,
                  StoryProviderImpl* const story_provider_impl,
                  const fidl::String& url,
                  const std::string& story_id,
                  FidlStringMap extra_info,
                  fidl::String root_json,
                  Result result)
      : Operation(container),
        ledger_(ledger),
        story_provider_impl_(story_provider_impl),
        url_(url),
        story_id_(story_id),
        extra_info_(std::move(extra_info)),
        root_json_(std::move(root_json)),
        result_(result) {
    Ready();
  }

  void Run() override {
    ledger_->NewPage(story_page_.NewRequest(), [this](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "CreateStoryCall() " << story_id_
                       << " Ledger.NewPage() " << status;
        Done();
        return;
      }

      story_page_->GetId([this](fidl::Array<uint8_t> story_page_id) {
        story_data_ = StoryData::New();
        story_data_->story_page_id = std::move(story_page_id);
        story_data_->story_info = StoryInfo::New();
        auto* const story_info = story_data_->story_info.get();
        story_info->url = url_;
        story_info->id = story_id_;
        story_info->is_running = false;
        story_info->state = StoryState::INITIAL;
        story_info->extra = std::move(extra_info_);
        story_info->extra.mark_non_null();

        story_provider_impl_->WriteStoryData(story_data_->Clone(), [this]() {
          controller_ = std::make_unique<StoryImpl>(
              std::move(story_data_), story_provider_impl_);

          // We call stop on the controller to ensure that root data has been
          // written before this operations is done.
          controller_->AddLinkDataAndSync(std::move(root_json_), [this] {
            result_(story_id_);
            Done();
          });
        });
      });
    });
  }

 private:
  ledger::Ledger* const ledger_;                  // not owned
  StoryProviderImpl* const story_provider_impl_;  // not owned
  const fidl::String url_;
  const std::string story_id_;
  FidlStringMap extra_info_;
  fidl::String root_json_;
  Result result_;

  ledger::PagePtr story_page_;
  StoryDataPtr story_data_;
  std::unique_ptr<StoryImpl> controller_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CreateStoryCall);
};

class DeleteStoryCall : public Operation {
 public:
  using Result = StoryProviderImpl::DeleteStoryCallback;
  using StoryIdSet = std::unordered_set<std::string>;
  using ControllerMap =
      std::unordered_map<std::string, std::unique_ptr<StoryImpl>>;
  using PendingDeletion = std::pair<std::string, DeleteStoryCall*>;

  DeleteStoryCall(OperationContainer* const container,
                  ledger::Ledger* const ledger,
                  const fidl::String& story_id,
                  StoryIdSet* const story_ids,
                  ControllerMap* const story_controllers,
                  PendingDeletion* const pending_deletion,
                  Result result)
      : Operation(container),
        ledger_(ledger),
        story_id_(story_id),
        story_ids_(story_ids),
        story_controllers_(story_controllers),
        pending_deletion_(pending_deletion),
        result_(result) {
    Ready();
  }

  void Run() override {
    if (pending_deletion_ == nullptr) {
      Complete();
      return;
    }

    // There should not be an existing pending_deletion_.
    FTL_DCHECK(pending_deletion_->first.empty());
    FTL_DCHECK(pending_deletion_->second == nullptr);
    *pending_deletion_ = std::make_pair(story_id_, this);

    ledger_->GetRootPage(root_page_.NewRequest(), [this](
        ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "DeleteStoryCall() " << story_id_
                       << " Ledger.GetRootPage() " << status;
        *pending_deletion_ = std::pair<std::string, DeleteStoryCall*>();
        result_();
        Done();
        return;
      }

      root_page_->Delete(to_array(story_id_), [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "DeleteStoryCall() " << story_id_
                         << " Page.Delete() " << status;
        }
      });
    });

    // Complete() would be trigerred by PageWatcher::OnChange().
  }

  void Complete() {
    story_ids_->erase(story_id_);
    if (pending_deletion_) {
      *pending_deletion_ = std::pair<std::string, DeleteStoryCall*>();
    }

    auto i = story_controllers_->find(story_id_);
    if (i == story_controllers_->end()) {
      result_();
      Done();
      return;
    }

    FTL_DCHECK(i->second.get() != nullptr);
    i->second->StopForDelete([this] {
      story_controllers_->erase(story_id_);
      result_();
      Done();
    });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  ledger::PagePtr root_page_;
  const fidl::String story_id_;
  StoryIdSet* const story_ids_;
  ControllerMap* const story_controllers_;
  PendingDeletion* const pending_deletion_;
  Result result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeleteStoryCall);
};

class GetControllerCall : public Operation {
 public:
  using ControllerMap =
      std::unordered_map<std::string, std::unique_ptr<StoryImpl>>;

  GetControllerCall(OperationContainer* const container,
                    ledger::Ledger* const ledger,
                    StoryProviderImpl* const story_provider_impl,
                    ControllerMap* const story_controllers,
                    const fidl::String& story_id,
                    fidl::InterfaceRequest<StoryController> request)
      : Operation(container),
        ledger_(ledger),
        story_provider_impl_(story_provider_impl),
        story_controllers_(story_controllers),
        story_id_(story_id),
        request_(std::move(request)) {
    Ready();
  }

  void Run() override {
    // If possible, try connecting to an existing controller.
    auto i = story_controllers_->find(story_id_);
    if (i != story_controllers_->end()) {
      i->second->Connect(std::move(request_));
      Done();
      return;
    }

    story_provider_impl_->GetStoryData(story_id_, [this](
                                                      StoryDataPtr story_data) {
      if (story_data.is_null()) {
        // We cannot resume a deleted (or otherwise non-existing) story.
        Done();
        return;
      }
      story_data_ = std::move(story_data);
      ledger_->GetPage(story_data_->story_page_id.Clone(),
                       story_page_.NewRequest(), [this](ledger::Status status) {
                         if (status != ledger::Status::OK) {
                           FTL_LOG(ERROR) << "GetControllerCall() "
                                          << story_data_->story_info->id
                                          << " Ledger.GetPage() " << status;
                         }
                         auto controller = new StoryImpl(
                             std::move(story_data_), story_provider_impl_);
                         controller->Connect(std::move(request_));
                         story_controllers_->emplace(story_id_, controller);
                         Done();
                       });
    });
  }

 private:
  ledger::Ledger* const ledger_;                  // not owned
  StoryProviderImpl* const story_provider_impl_;  // not owned
  ControllerMap* const story_controllers_;
  const fidl::String story_id_;
  fidl::InterfaceRequest<StoryController> request_;

  StoryDataPtr story_data_;
  ledger::PagePtr story_page_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetControllerCall);
};

class PreviousStoriesCall : public Operation {
 public:
  using Result = StoryProviderImpl::PreviousStoriesCallback;

  PreviousStoriesCall(OperationContainer* const container,
                      ledger::Ledger* const ledger,
                      Result result)
      : Operation(container), ledger_(ledger), result_(result) {
    Ready();
  }

  void Run() override {
    // This resize() has the side effect of marking the array as
    // non-null. Do not remove it because the fidl declaration
    // of this return value does not allow nulls.
    story_ids_.resize(0);

    ledger_->GetRootPage(root_page_.NewRequest(), [this](
        ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "PreviousStoryCall() "
                       << " Ledger.GetRootPage() " << status;
        result_(std::move(story_ids_));
        Done();
        return;
      }
      root_page_->GetSnapshot(
          root_snapshot_.NewRequest(), nullptr, [this](ledger::Status status) {
            if (status != ledger::Status::OK) {
              FTL_LOG(ERROR) << "PreviousStoryCall() "
                             << " Page.GetSnapshot() " << status;
              result_(std::move(story_ids_));
              Done();
              return;
            }
            root_snapshot_->GetEntries(
                nullptr, nullptr, [this](ledger::Status status,
                                         fidl::Array<ledger::EntryPtr> entries,
                                         fidl::Array<uint8_t> next_token) {
                  if (status != ledger::Status::OK) {
                    FTL_LOG(ERROR) << "PreviousStoryCall() "
                                   << " PageSnapshot.GetEntries() " << status;
                    result_(std::move(story_ids_));
                    Done();
                    return;
                  }

                  // TODO(mesch): Account for possible
                  // continuation here. That's not just a matter
                  // of repeatedly calling, but it needs to be
                  // wired up to the API, because a list that is
                  // too large to return from Ledger is also too
                  // large to return from StoryProvider.

                  for (auto& entry : entries) {
                    StoryDataPtr story_data = StoryData::New();
                    story_data->Deserialize(entry->value->get_bytes().data(),
                                            entry->value->get_bytes().size());
                    story_ids_.push_back(story_data->story_info->id);
                  }

                  result_(std::move(story_ids_));
                  Done();
                });
          });
    });
  }

 private:
  ledger::Ledger* const ledger_;  // not owned
  fidl::Array<fidl::String> story_ids_;
  Result result_;
  ledger::PagePtr root_page_;
  ledger::PageSnapshotPtr root_snapshot_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PreviousStoriesCall);
};

}  // namespace

StoryProviderImpl::StoryProviderImpl(
    ApplicationEnvironmentPtr environment,
    fidl::InterfaceHandle<ledger::Ledger> ledger,
    ledger::LedgerRepositoryPtr ledger_repository)
    : environment_(std::move(environment)),
      storage_(new Storage),
      page_watcher_binding_(this),
      ledger_repository_(std::move(ledger_repository)) {
  environment_->GetApplicationLauncher(launcher_.NewRequest());

  ledger_.Bind(std::move(ledger));

  ledger::PagePtr root_page;
  ledger_->GetRootPage(root_page.NewRequest(), [this](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR)
          << "StoryProviderImpl() failed call to Ledger.GetRootPage() "
          << status;
    }
  });

  fidl::InterfaceHandle<ledger::PageWatcher> watcher;
  page_watcher_binding_.Bind(watcher.NewRequest());
  // TODO(mesch): Consider to initialize story_ids_ here. OnChange watcher
  // callbacks may be from an unknown base state if we don't use the snapshot
  // here.
  ledger::PageSnapshotPtr snapshot_unused;
  root_page->GetSnapshot(snapshot_unused.NewRequest(), std::move(watcher),
    [](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "StoryProviderImpl() failed call to Ledger.Watch() "
                       << status;
      }
  });

  // We must initialize story_ids_ with the IDs of currently existing
  // stories *before* we can process any calls that might create a new
  // story. Hence we bind the interface request only after this call
  // completes.
  new PreviousStoriesCall(&operation_queue_, ledger_.get(), ftl::MakeCopyable(
      [this](fidl::Array<fidl::String> stories) mutable {
    for (auto& story_id : stories) {
      story_ids_.insert(story_id.get());
    }

    InitStoryId();  // So MakeStoryId() returns something more random.

    for (auto& request : requests_) {
      bindings_.AddBinding(this, std::move(request));
    }
    requests_.clear();
    ready_ = true;
  }));
}

StoryProviderImpl::~StoryProviderImpl() {}

void StoryProviderImpl::AddBinding(fidl::InterfaceRequest<StoryProvider> request) {
  if (ready_) {
    bindings_.AddBinding(this, std::move(request));
  } else {
    requests_.emplace_back(std::move(request));
  }
}

void StoryProviderImpl::PurgeController(const std::string& story_id) {
  story_controllers_.erase(story_id);
}

// |StoryProvider|
void StoryProviderImpl::Watch(
    fidl::InterfaceHandle<StoryProviderWatcher> watcher) {
  watchers_.AddInterfacePtr(
      StoryProviderWatcherPtr::Create(std::move(watcher)));
}

void StoryProviderImpl::GetStoryData(
    const fidl::String& story_id,
    const std::function<void(StoryDataPtr)>& result) {
  new GetStoryDataCall(&operation_collection_, ledger_.get(), story_id, result);
}

ledger::PagePtr StoryProviderImpl::GetStoryPage(
    const fidl::Array<uint8_t>& story_page_id) {
  ledger::PagePtr ret;
  ledger_->GetPage(story_page_id.Clone(), ret.NewRequest(),
                   [](ledger::Status status) {
                     if (status != ledger::Status::OK) {
                       FTL_LOG(ERROR) << "GetStoryPage() status " << status;
                     }
                   });

  return ret;
}

void StoryProviderImpl::ConnectToResolver(
    fidl::InterfaceRequest<Resolver> request) {
  if (!resolver_services_.is_bound()) {
    auto resolver_launch_info = ApplicationLaunchInfo::New();
    resolver_launch_info->services = resolver_services_.NewRequest();
    resolver_launch_info->url = "file:///system/apps/resolver";
    ApplicationControllerPtr app;
    launcher_->CreateApplication(std::move(resolver_launch_info),
                                 app.NewRequest());
    apps_.AddInterfacePtr(std::move(app));
  }
  ConnectToService(resolver_services_.get(), std::move(request));
}

void StoryProviderImpl::WriteStoryData(StoryDataPtr story_data,
                                       std::function<void()> done) {
  new WriteStoryDataCall(&operation_collection_, ledger_.get(),
                         std::move(story_data), done);
}

// |StoryProvider|
void StoryProviderImpl::CreateStory(const fidl::String& url,
                                    const CreateStoryCallback& callback) {
  const std::string story_id = MakeStoryId(&story_ids_, 10);
  FTL_LOG(INFO) << "CreateStory() " << url;
  new CreateStoryCall(&operation_queue_, ledger_.get(), this, url, story_id,
                      FidlStringMap(), fidl::String(), callback);
}

// |StoryProvider|
void StoryProviderImpl::CreateStoryWithInfo(
    const fidl::String& url,
    FidlStringMap extra_info,
    const fidl::String& root_json,
    const CreateStoryWithInfoCallback& callback) {
  const std::string story_id = MakeStoryId(&story_ids_, 10);
  FTL_LOG(INFO) << "CreateStoryWithInfo() " << root_json;
  new CreateStoryCall(&operation_queue_, ledger_.get(), this, url, story_id,
                      std::move(extra_info), std::move(root_json), callback);
}

// |StoryProvider|
void StoryProviderImpl::DeleteStory(const fidl::String& story_id,
                                    const DeleteStoryCallback& callback) {
  new DeleteStoryCall(&operation_queue_, ledger_.get(), story_id, &story_ids_,
                      &story_controllers_, &pending_deletion_, callback);
}

// |StoryProvider|
void StoryProviderImpl::GetStoryInfo(
    const fidl::String& story_id,
    const GetStoryInfoCallback& callback) {
  new GetStoryDataCall(
      &operation_collection_, ledger_.get(), story_id,
      [this, callback](StoryDataPtr story_data) {
        callback(
            story_data.is_null() ? nullptr : std::move(story_data->story_info));
      });
}

// |StoryProvider|
void StoryProviderImpl::GetController(
    const fidl::String& story_id,
    fidl::InterfaceRequest<StoryController> request) {
  new GetControllerCall(&operation_queue_, ledger_.get(), this,
                        &story_controllers_, story_id, std::move(request));
}

// |StoryProvider|
void StoryProviderImpl::PreviousStories(
    const PreviousStoriesCallback& callback) {
  new PreviousStoriesCall(&operation_queue_, ledger_.get(), callback);
}

// |PageWatcher|
void StoryProviderImpl::OnChange(ledger::PageChangePtr page,
                                 const OnChangeCallback& callback) {
  FTL_DCHECK(!page.is_null());
  FTL_DCHECK(!page->changes.is_null());

  for (auto& entry : page->changes) {
    auto story_data = StoryData::New();
    auto& bytes = entry->value->get_bytes();
    story_data->Deserialize(bytes.data(), bytes.size());

    // If this is a new story, guard against double using its key.
    story_ids_.insert(story_data->story_info->id.get());

    watchers_.ForAllPtrs([&story_data](StoryProviderWatcher* const watcher) {
      watcher->OnChange(story_data->story_info.Clone());
    });
  }

  for (auto& key : page->deleted_keys) {
    const fidl::String story_id = to_string(key);
    watchers_.ForAllPtrs([&story_id](StoryProviderWatcher* const watcher) {
      watcher->OnDelete(story_id);
    });

    if (pending_deletion_.first == story_id) {
      pending_deletion_.second->Complete();
    } else {
      new DeleteStoryCall(&operation_queue_, ledger_.get(), story_id,
                          &story_ids_, &story_controllers_,
                          nullptr /* pending_deletion */, [] {});
    }
  }

  callback(nullptr);
}

}  // namespace modular
