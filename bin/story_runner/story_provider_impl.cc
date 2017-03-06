// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_provider_impl.h"

#include <stdlib.h>
#include <time.h>
#include <unordered_set>

#include "application/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/src/story_runner/story_impl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/mtl/tasks/message_loop.h"

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

void GetEntries(ledger::PageSnapshotPtr* snapshot,
                std::vector<ledger::EntryPtr> entries,
                fidl::Array<uint8_t> token,
                std::function<void(ledger::Status,
                                   std::vector<ledger::EntryPtr>)> callback) {
  (*snapshot)->GetEntries(
      nullptr, std::move(token), ftl::MakeCopyable([
        snapshot, entries = std::move(entries), callback = std::move(callback)
      ](ledger::Status status, auto new_entries, auto next_token) mutable {
        if (status != ledger::Status::OK &&
            status != ledger::Status::PARTIAL_RESULT) {
          callback(status, {});
          return;
        }
        for (auto& entry : new_entries) {
          entries.push_back(std::move(entry));
        }
        if (status == ledger::Status::OK) {
          callback(ledger::Status::OK, std::move(entries));
          return;
        }
        GetEntries(snapshot, std::move(entries), std::move(next_token),
                   std::move(callback));
      }));
}

// Retrieves all entries from the given snapshot and calls the given callback
// with the returned status and entry vector.
void GetEntries(ledger::PageSnapshotPtr* snapshot,
                std::function<void(ledger::Status,
                                   std::vector<ledger::EntryPtr>)> callback) {
  GetEntries(snapshot, {}, nullptr, std::move(callback));
}

// Below are helper classes that encapsulate a chain of asynchronous
// operations on the Ledger. Because the operations all return
// something, the handles on which they are invoked need to be kept
// around until the return value arrives. This precludes them to be
// local variables. The next thing that comes to mind is to make them
// fields of the containing object. However, there might be multiple
// such operations going on concurrently in one StoryProviderImpl
// (although right now there are not, because they are all serialized
// in one operation queue), so they cannot be fields of
// StoryProviderImpl. Thus such operations are separate classes.

class GetStoryDataCall : public Operation<StoryDataPtr> {
 public:
  GetStoryDataCall(OperationContainer* const container,
                   std::shared_ptr<ledger::PageSnapshotPtr> root_snapshot,
                   const fidl::String& story_id,
                   ResultCall result_call)
      : Operation(container, std::move(result_call)),
        root_snapshot_(std::move(root_snapshot)),
        story_id_(story_id) {
    Ready();
  }

  void Run() override {
    (*root_snapshot_)
        ->Get(to_array(story_id_),
              [this](ledger::Status status, ledger::ValuePtr value) {
                if (status != ledger::Status::OK) {
                  FTL_LOG(ERROR) << "GetStoryDataCall() " << story_id_
                                 << " PageSnapshot.Get() " << status;
                  Done(std::move(story_data_));
                  return;
                }

                story_data_ = StoryData::New();
                story_data_->Deserialize(value->get_bytes().data(),
                                         value->get_bytes().size());

                Done(std::move(story_data_));
              });
  };

 private:
  std::shared_ptr<ledger::PageSnapshotPtr> root_snapshot_;
  const fidl::String story_id_;
  StoryDataPtr story_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetStoryDataCall);
};

class WriteStoryDataCall : public Operation<void> {
 public:
  WriteStoryDataCall(OperationContainer* const container,
                     ledger::Page* const root_page,
                     StoryDataPtr story_data,
                     ResultCall result_call)
      : Operation(container, std::move(result_call)),
        root_page_(root_page),
        story_data_(std::move(story_data)) {
    Ready();
  }

  void Run() override {
    FTL_DCHECK(!story_data_.is_null());

    const size_t size = story_data_->GetSerializedSize();
    fidl::Array<uint8_t> value = fidl::Array<uint8_t>::New(size);
    story_data_->Serialize(value.data(), size);

    root_page_->PutWithPriority(
        to_array(story_data_->story_info->id), std::move(value),
        ledger::Priority::EAGER, [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            const fidl::String& story_id = story_data_->story_info->id;
            FTL_LOG(ERROR) << "WriteStoryDataCall() " << story_id
                           << " Page.PutWithPriority() " << status;
          }

          Done();
        });
  }

 private:
  ledger::Page* const root_page_;  // not owned
  StoryDataPtr story_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteStoryDataCall);
};

class CreateStoryCall : public Operation<fidl::String> {
 public:
  using FidlStringMap = StoryProviderImpl::FidlStringMap;

  CreateStoryCall(OperationContainer* const container,
                  ledger::Ledger* const ledger,
                  ledger::Page* const root_page,
                  StoryProviderImpl* const story_provider_impl,
                  const fidl::String& url,
                  const fidl::String& story_id,
                  FidlStringMap extra_info,
                  fidl::String root_json,
                  ResultCall result_call)
      : Operation(container, std::move(result_call)),
        ledger_(ledger),
        root_page_(root_page),
        story_provider_impl_(story_provider_impl),
        url_(url),
        story_id_(story_id),
        extra_info_(std::move(extra_info)),
        root_json_(std::move(root_json)) {
    Ready();
  }

  void Run() override {
    ledger_->NewPage(
        story_page_.NewRequest(),
        [this](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "CreateStoryCall() " << story_id_
                           << " Ledger.NewPage() " << status;
            Done(std::move(story_id_));
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

            new WriteStoryDataCall(
                &operation_queue_, root_page_, story_data_->Clone(),
                [this] { Cont(); });
          });
        });
  }

  void Cont() {
    controller_ = std::make_unique<StoryImpl>(std::move(story_data_),
                                              story_provider_impl_);

    // We ensure that root data has been written before this operations is
    // done.
    controller_->AddLinkDataAndSync(std::move(root_json_), [this] {
      Done(std::move(story_id_));
    });
  }

 private:
  ledger::Ledger* const ledger_;                  // not owned
  ledger::Page* const root_page_;                 // not owned
  StoryProviderImpl* const story_provider_impl_;  // not owned
  const fidl::String url_;
  fidl::String story_id_;
  FidlStringMap extra_info_;
  fidl::String root_json_;

  ledger::PagePtr story_page_;
  StoryDataPtr story_data_;
  std::unique_ptr<StoryImpl> controller_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(CreateStoryCall);
};

class DeleteStoryCall : public Operation<void> {
 public:
  using StoryIdSet = std::unordered_set<std::string>;
  using ControllerMap =
      std::unordered_map<std::string, std::unique_ptr<StoryImpl>>;
  using PendingDeletion = std::pair<std::string, DeleteStoryCall*>;

  DeleteStoryCall(OperationContainer* const container,
                  ledger::Page* const root_page,
                  const fidl::String& story_id,
                  StoryIdSet* const story_ids,
                  ControllerMap* const story_controllers,
                  PendingDeletion* const pending_deletion,
                  ResultCall result_call)
      : Operation(container, std::move(result_call)),
        root_page_(root_page),
        story_id_(story_id),
        story_ids_(story_ids),
        story_controllers_(story_controllers),
        pending_deletion_(pending_deletion) {
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

    root_page_->Delete(to_array(story_id_), [this](ledger::Status status) {
      if (status != ledger::Status::OK) {
        FTL_LOG(ERROR) << "DeleteStoryCall() " << story_id_ << " Page.Delete() "
                       << status;
      }
    });
    // Complete() is called by PageWatcher::OnChange().
  }

  void Complete() {
    story_ids_->erase(story_id_);
    if (pending_deletion_) {
      *pending_deletion_ = std::pair<std::string, DeleteStoryCall*>();
    }

    auto i = story_controllers_->find(story_id_);
    if (i == story_controllers_->end()) {
      Done();
      return;
    }

    FTL_DCHECK(i->second.get() != nullptr);
    i->second->StopForDelete([this] {
      story_controllers_->erase(story_id_);
      Done();
    });
  }

 private:
  ledger::Page* const root_page_;  // not owned
  const fidl::String story_id_;
  StoryIdSet* const story_ids_;
  ControllerMap* const story_controllers_;
  PendingDeletion* const pending_deletion_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeleteStoryCall);
};

class GetControllerCall : public Operation<void> {
 public:
  using ControllerMap =
      std::unordered_map<std::string, std::unique_ptr<StoryImpl>>;

  GetControllerCall(OperationContainer* const container,
                    ledger::Ledger* const ledger,
                    ledger::Page* const root_page,
                    std::shared_ptr<ledger::PageSnapshotPtr> root_snapshot,
                    StoryProviderImpl* const story_provider_impl,
                    ControllerMap* const story_controllers,
                    const fidl::String& story_id,
                    fidl::InterfaceRequest<StoryController> request)
      : Operation(container, []{}),
        ledger_(ledger),
        root_page_(root_page),
        root_snapshot_(std::move(root_snapshot)),
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

    new GetStoryDataCall(&operation_queue_, root_snapshot_, story_id_,
                         [this](StoryDataPtr story_data) {
                           story_data_ = std::move(story_data);
                           Cont1();
                         });
  }

  void Cont1() {
    if (story_data_.is_null()) {
      // We cannot resume a deleted (or otherwise non-existing) story.
      Done();
      return;
    }

    // HACK(mesch): If the story were really running, it would
    // have a story controller found in the section above, and
    // we would never get here. But if the user runner was
    // previously killed while the story was running, the story
    // would be recorded in the ledger as running even thoug it
    // isn't, and the user shell is then unable to actually
    // start it (cf. StoryImpl::Start()).
    //
    // This needs to be fixed properly in different ways (adding
    // a device ID to the persisted state and resurrecting the
    // user session with stories already running). This
    // workaround here just gets user shell be able to start
    // previous stories. FW-95
    //
    // If this field is changed here, it needs to be written back too,
    // otherwise StoryProvider.GetStoryInfo() and
    // StoryController.GetInfo() will return the wrong values.
    if (story_data_->story_info->is_running) {
      FTL_LOG(INFO) << "GetControllerCall() " << story_data_->story_info->id
                    << " marked running but isn't -- correcting";
      story_data_->story_info->is_running = false;

      new WriteStoryDataCall(&operation_queue_, root_page_,
                             story_data_->Clone(),
                             [this] { Cont2(); });
    } else {
      Cont2();
    }
  }

  void Cont2() {
    ledger_->GetPage(story_data_->story_page_id.Clone(),
                     story_page_.NewRequest(), [this](ledger::Status status) {
                       if (status != ledger::Status::OK) {
                         FTL_LOG(ERROR) << "GetControllerCall() "
                                        << story_data_->story_info->id
                                        << " Ledger.GetPage() " << status;
                       }
                       auto controller = new StoryImpl(std::move(story_data_),
                                                       story_provider_impl_);
                       controller->Connect(std::move(request_));
                       story_controllers_->emplace(story_id_, controller);
                       Done();
                     });
  }

 private:
  ledger::Ledger* const ledger_;   // not owned
  ledger::Page* const root_page_;  // not owned
  std::shared_ptr<ledger::PageSnapshotPtr> root_snapshot_;
  StoryProviderImpl* const story_provider_impl_;  // not owned
  ControllerMap* const story_controllers_;
  const fidl::String story_id_;
  fidl::InterfaceRequest<StoryController> request_;

  StoryDataPtr story_data_;
  ledger::PagePtr story_page_;

  // Sub operations run in this queue.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GetControllerCall);
};

class PreviousStoriesCall : public Operation<fidl::Array<fidl::String>> {
 public:
  PreviousStoriesCall(OperationContainer* const container,
                      std::shared_ptr<ledger::PageSnapshotPtr> root_snapshot,
                      ResultCall result_call)
      : Operation(container, std::move(result_call)),
        root_snapshot_(std::move(root_snapshot)) {
    Ready();
  }

  void Run() override {
    // This resize() has the side effect of marking the array as
    // non-null. Do not remove it because the fidl declaration
    // of this return value does not allow nulls.
    story_ids_.resize(0);

    GetEntries(
        root_snapshot_.get(),
        [this](ledger::Status status, std::vector<ledger::EntryPtr> entries) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR) << "PreviousStoryCall() "
                           << " PageSnapshot.GetEntries() " << status;
            Done(std::move(story_ids_));
            return;
          }

          // TODO(mesch): Pagination might be needed here. If the list
          // of entries returned from the Ledger is too large, it might
          // also be too large to return from StoryProvider.

          for (auto& entry : entries) {
            // TODO(mesch): Not a good idea to mix keys of
            // different kinds in the same page. Once we are
            // more comfortable dealing with JSON data, we can
            // make a better mapping of a complex data
            // structure to a page.
            if (to_string(entry->key) == kDeviceMapKey) {
              continue;
            }

            StoryDataPtr story_data = StoryData::New();
            story_data->Deserialize(entry->value->get_bytes().data(),
                                    entry->value->get_bytes().size());
            story_ids_.push_back(story_data->story_info->id);
            FTL_LOG(INFO) << "PreviousStoryCall() "
                          << " previous story " << story_data->story_info->id
                          << " " << story_data->story_info->url << " "
                          << story_data->story_info->is_running;
          }

          Done(std::move(story_ids_));
        });
  }

 private:
  std::shared_ptr<ledger::PageSnapshotPtr> root_snapshot_;
  fidl::Array<fidl::String> story_ids_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PreviousStoriesCall);
};

class UpdateDeviceNameCall : public Operation<void> {
 public:
  UpdateDeviceNameCall(OperationContainer* const container,
                       ledger::Page* const root_page,
                       std::shared_ptr<ledger::PageSnapshotPtr> root_snapshot,
                       const std::string& device_name)
      : Operation(container, [] {}),
        root_page_(root_page),
        root_snapshot_(std::move(root_snapshot)),
        device_name_(device_name) {
    Ready();
  }

  void Run() override {
    (*root_snapshot_)
        ->Get(to_array(kDeviceMapKey),
              [this](ledger::Status status, ledger::ValuePtr value) {
                if (status != ledger::Status::OK &&
                    status != ledger::Status::KEY_NOT_FOUND) {
                  FTL_LOG(ERROR) << "UpdateDeviceNameCall() "
                                 << " PageSnapshot.Get() " << status;
                  Done();
                  return;
                }

                rapidjson::Document doc;
                if (!value.is_null()) {
                  doc.Parse(to_string(value->get_bytes()));
                  FTL_DCHECK(doc.IsObject());
                } else {
                  doc.SetObject();
                }

                // NOTE(mesch): Unclear why just device_name_ as
                // the key doesn't compile.
                doc.AddMember(rapidjson::StringRef(device_name_), true,
                              doc.GetAllocator());

                root_page_->Put(to_array(kDeviceMapKey),
                                to_array(JsonValueToString(doc)),
                                [this](ledger::Status status) {
                                  if (status != ledger::Status::OK) {
                                    FTL_LOG(ERROR) << "UpdateDeviceNameCall() "
                                                   << " Page.Put() " << status;
                                  }

                                  Done();
                                });
              });
  }

 private:
  ledger::Page* const root_page_;  // not owned
  std::shared_ptr<ledger::PageSnapshotPtr> root_snapshot_;
  const std::string device_name_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UpdateDeviceNameCall);
};

}  // namespace

StoryProviderImpl::StoryProviderImpl(
    app::ApplicationEnvironmentPtr environment,
    fidl::InterfaceHandle<ledger::Ledger> ledger,
    const std::string& device_name,
    const ComponentContextInfo& component_context_info)
    : environment_(std::move(environment)),
      storage_(new Storage),
      page_watcher_binding_(this),
      component_context_info_(component_context_info) {
  environment_->GetApplicationLauncher(launcher_.NewRequest());

  ledger_.Bind(std::move(ledger));

  ledger_->SetConflictResolverFactory(
      conflict_resolver_.AddBinding(), [](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "StoryProviderImpl() failed call to "
                         << "Ledger.SetConflictResolverFactory() " << status;
        }
      });

  ledger_->GetRootPage(root_page_.NewRequest(), [](ledger::Status status) {
    if (status != ledger::Status::OK) {
      FTL_LOG(ERROR)
          << "StoryProviderImpl() failed call to Ledger.GetRootPage() "
          << status;
    }
  });

  root_page_->GetSnapshot(
      ResetRootSnapshot(), page_watcher_binding_.NewBinding(),
      [](ledger::Status status) {
        if (status != ledger::Status::OK) {
          FTL_LOG(ERROR) << "StoryProviderImpl() failed call to Ledger.GetSnapshot() "
                         << status;
        }
      });

  // Record the device name of the current device in the ledger,
  // before we handle any requests.
  new UpdateDeviceNameCall(&operation_queue_, root_page_.get(), root_snapshot_,
                           device_name);

  // We must initialize story_ids_ with the IDs of currently existing
  // stories *before* we can process any calls that might create a new
  // story. Hence we bind the interface request only after this call
  // completes.
  new PreviousStoriesCall(
      &operation_queue_, root_snapshot_,
      [this](fidl::Array<fidl::String> stories) {
        for (auto& story_id : stories) {
          story_ids_.insert(story_id.get());
        }

        InitStoryId();  // So MakeStoryId() returns something more random.

        for (auto& request : requests_) {
          bindings_.AddBinding(this, std::move(request));
        }
        requests_.clear();
        ready_ = true;
      });
}

StoryProviderImpl::~StoryProviderImpl() = default;

void StoryProviderImpl::AddBinding(
    fidl::InterfaceRequest<StoryProvider> request) {
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
  new GetStoryDataCall(&operation_queue_, root_snapshot_, story_id, result);
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

void StoryProviderImpl::WriteStoryData(StoryDataPtr story_data,
                                       std::function<void()> done) {
  new WriteStoryDataCall(&operation_queue_, root_page_.get(),
                         std::move(story_data), done);
}

// |StoryProvider|
void StoryProviderImpl::CreateStory(const fidl::String& url,
                                    const CreateStoryCallback& callback) {
  const std::string story_id = MakeStoryId(&story_ids_, 10);
  FTL_LOG(INFO) << "CreateStory() " << url;
  new CreateStoryCall(&operation_queue_, ledger_.get(), root_page_.get(), this,
                      url, story_id, FidlStringMap(), fidl::String(), callback);
}

// |StoryProvider|
void StoryProviderImpl::CreateStoryWithInfo(
    const fidl::String& url,
    FidlStringMap extra_info,
    const fidl::String& root_json,
    const CreateStoryWithInfoCallback& callback) {
  const std::string story_id = MakeStoryId(&story_ids_, 10);
  FTL_LOG(INFO) << "CreateStoryWithInfo() " << root_json;
  new CreateStoryCall(&operation_queue_, ledger_.get(), root_page_.get(), this,
                      url, story_id, std::move(extra_info),
                      std::move(root_json), callback);
}

// |StoryProvider|
void StoryProviderImpl::DeleteStory(const fidl::String& story_id,
                                    const DeleteStoryCallback& callback) {
  new DeleteStoryCall(&operation_queue_, root_page_.get(), story_id,
                      &story_ids_, &story_controllers_, &pending_deletion_,
                      callback);
}

// |StoryProvider|
void StoryProviderImpl::GetStoryInfo(const fidl::String& story_id,
                                     const GetStoryInfoCallback& callback) {
  new GetStoryDataCall(&operation_queue_, root_snapshot_, story_id,
                       [this, callback](StoryDataPtr story_data) {
                         callback(story_data.is_null()
                                      ? nullptr
                                      : std::move(story_data->story_info));
                       });
}

// |StoryProvider|
void StoryProviderImpl::GetController(
    const fidl::String& story_id,
    fidl::InterfaceRequest<StoryController> request) {
  new GetControllerCall(&operation_queue_, ledger_.get(), root_page_.get(),
                        root_snapshot_, this, &story_controllers_, story_id,
                        std::move(request));
}

// |StoryProvider|
void StoryProviderImpl::PreviousStories(
    const PreviousStoriesCallback& callback) {
  new PreviousStoriesCall(&operation_queue_, root_snapshot_, callback);
}

// |PageWatcher|
void StoryProviderImpl::OnChange(ledger::PageChangePtr page,
                                 const OnChangeCallback& callback) {
  FTL_DCHECK(!page.is_null());
  FTL_DCHECK(!page->changes.is_null());

  for (auto& entry : page->changes) {
    // TODO(mesch): See PreviousStoriesCall().
    if (to_string(entry->key) == kDeviceMapKey) {
      continue;
    }

    auto story_data = StoryData::New();
    auto& bytes = entry->value->get_bytes();
    story_data->Deserialize(bytes.data(), bytes.size());

    // If this is a new story, guard against double using its key.
    story_ids_.insert(story_data->story_info->id.get());

    watchers_.ForAllPtrs([&story_data](StoryProviderWatcher* const watcher) {
      watcher->OnChange(story_data->story_info.Clone());
    });

    // TODO(mesch): If there is an update for a running story, the story
    // controller needs to be notified.
  }

  for (auto& key : page->deleted_keys) {
    const fidl::String story_id = to_string(key);
    watchers_.ForAllPtrs([&story_id](StoryProviderWatcher* const watcher) {
      watcher->OnDelete(story_id);
    });

    if (pending_deletion_.first == story_id) {
      pending_deletion_.second->Complete();
    } else {
      new DeleteStoryCall(&operation_queue_, root_page_.get(), story_id,
                          &story_ids_, &story_controllers_,
                          nullptr /* pending_deletion */, [] {});
    }
  }

  // Every time we receive an OnChange notification, we update the
  // root page snapshot so we see the current state. Note that pending
  // Operation instances hold on to the previous value until they
  // finish. New Operation instances created after the update receive
  // the new snapshot.
  callback(ResetRootSnapshot());
}

fidl::InterfaceRequest<ledger::PageSnapshot> StoryProviderImpl::ResetRootSnapshot() {
  root_snapshot_.reset(new ledger::PageSnapshotPtr);
  auto ret = (*root_snapshot_).NewRequest();
  (*root_snapshot_).set_connection_error_handler([this] {
    FTL_LOG(ERROR)
        << "StoryProviderImpl: PageSnapshot connection unexpectedly closed.";
  });
  return ret;
}

}  // namespace modular
