// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/story_runner/story_storage_impl.h"

#include <memory>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/operation.h"
#include "apps/modular/services/story/story_storage.fidl.h"
#include "lib/mtl/vmo/vector.h"

namespace modular {

namespace {

class ReadLinkDataCall : public Operation<fidl::String> {
 public:
  ReadLinkDataCall(OperationContainer* const container,
                   std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot,
                   const fidl::String& link_id,
                   ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_snapshot_(std::move(page_snapshot)),
        link_id_(link_id) {
    Ready();
  }

  void Run() override {
    (*page_snapshot_)
        ->Get(to_array(link_id_), [this](ledger::Status status, mx::vmo value) {
          if (status != ledger::Status::OK) {
            if (status != ledger::Status::KEY_NOT_FOUND) {
              // It's expected that the key is not found when the link
              // is accessed for the first time. Don't log an error
              // then.
              FTL_LOG(ERROR) << "ReadLinkDataCall() " << link_id_
                             << " PageSnapshot.Get() " << status;
            }
            Done(fidl::String());
            return;
          }

          data_ = LinkData::New();
          if (value) {
            std::vector<char> value_as_vector;
            if (!mtl::VectorFromVmo(value, &value_as_vector)) {
              FTL_LOG(ERROR) << "Unable to extract data.";
              Done(nullptr);
              return;
            }
            data_->Deserialize(value_as_vector.data(), value_as_vector.size());
          }
          Done(std::move(data_->json));
        });
  }

 private:
  std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot_;
  const fidl::String link_id_;
  LinkDataPtr data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadLinkDataCall);
};

class WriteLinkDataCall : public Operation<void> {
 public:
  WriteLinkDataCall(OperationContainer* const container,
                    ledger::Page* const page,
                    const fidl::String& link_id,
                    const fidl::String& data,
                    ResultCall result_call)
      : Operation(container, std::move(result_call)),
        page_(page),
        link_id_(link_id) {
    data_ = LinkData::New();
    data_->json = data;
    Ready();
  }

  void Run() override {
    fidl::Array<uint8_t> bytes;
    bytes.resize(data_->GetSerializedSize());
    data_->Serialize(bytes.data(), bytes.size());

    page_->Put(to_array(link_id_), std::move(bytes),
               [this](ledger::Status status) {
                 if (status != ledger::Status::OK) {
                   FTL_LOG(ERROR) << "WriteLinkDataCall() " << link_id_
                                  << " Page.Put() " << status;
                 }
                 Done();
               });
  }

 private:
  ledger::Page* const page_;  // not owned
  const fidl::String link_id_;
  LinkDataPtr data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteLinkDataCall);
};

class SyncCall : public Operation<void> {
 public:
  SyncCall(OperationContainer* const container, ResultCall result_call)
      : Operation(container, std::move(result_call)) {
    Ready();
  }

  void Run() override { Done(); }

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(SyncCall);
};

}  // namespace

StoryStorageImpl::StoryStorageImpl(std::shared_ptr<Storage> storage,
                                   ledger::PagePtr story_page,
                                   const fidl::String& key)
    : page_watcher_binding_(this),
      key_(key),
      storage_(storage),
      // Comment out this initializer in order to switch to in-memory storage.
      story_page_(std::move(story_page)),
      story_snapshot_("StoryStorageImpl") {
  if (story_page_.is_bound()) {
    story_page_->GetSnapshot(
        story_snapshot_.NewRequest(), page_watcher_binding_.NewBinding(),
        [](ledger::Status status) {
          if (status != ledger::Status::OK) {
            FTL_LOG(ERROR)
                << "StoryStorageImpl() failed call to Ledger.GetSnapshot() "
                << status;
          }
        });
  }
}

StoryStorageImpl::~StoryStorageImpl() = default;

void StoryStorageImpl::ReadLinkData(const fidl::String& link_id,
                                    const DataCallback& callback) {
  if (story_page_.is_bound()) {
    new ReadLinkDataCall(&operation_queue_, story_snapshot_.shared_ptr(),
                         link_id, callback);

  } else {
    auto& story_data = (*storage_)[key_];
    auto i = story_data.find(link_id);
    if (i != story_data.end()) {
      callback(i->second);
    } else {
      callback(nullptr);
    }
  }
}

void StoryStorageImpl::WriteLinkData(const fidl::String& link_id,
                                     const fidl::String& data,
                                     const SyncCallback& callback) {
  if (story_page_.is_bound()) {
    new WriteLinkDataCall(&operation_queue_, story_page_.get(), link_id, data,
                          callback);

  } else {
    (*storage_)[key_][link_id] = data;
    callback();
  }
}

void StoryStorageImpl::WatchLink(const fidl::String& link_id,
                                 const DataCallback& watcher) {
  watchers_.emplace_back(std::make_pair(link_id, watcher));
}

void StoryStorageImpl::Sync(const SyncCallback& callback) {
  new SyncCall(&operation_queue_, callback);
}

// |PageWatcher|
void StoryStorageImpl::OnChange(ledger::PageChangePtr page,
                                const OnChangeCallback& callback) {
  if (!page.is_null() && !page->changes.is_null()) {
    for (auto& entry : page->changes) {
      const fidl::String link_id = to_string(entry->key);
      for (auto& watcher_entry : watchers_) {
        if (link_id == watcher_entry.first) {
          auto data = LinkData::New();
          std::vector<char> value_as_vector;
          if (!mtl::VectorFromVmo(entry->value, &value_as_vector)) {
            FTL_LOG(ERROR) << "Unable to extract data.";
            continue;
          }
          data->Deserialize(value_as_vector.data(), value_as_vector.size());
          watcher_entry.second(data->json);
        }
      }
    }
  }

  // Every time we receive an OnChange notification, we update the
  // story page snapshot so we see the current state. Note that
  // pending Operation instances hold on to the previous value until
  // they finish. New Operation instances created after the update
  // receive the new snapshot.
  callback(story_snapshot_.NewRequest());
}

}  // namespace modular
