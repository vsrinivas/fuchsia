// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/story_storage_impl.h"

#include "apps/ledger/services/public/ledger.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/src/user_runner/transaction.h"

namespace modular {

namespace {

class ReadLinkDataCall : public Transaction {
 public:
  using Result = StoryStorageImpl::ReadLinkDataCallback;

  ReadLinkDataCall(TransactionContainer* const container,
                   ledger::Page* const page,
                   const fidl::String& link_id,
                   Result result)
      : Transaction(container),
        page_(page),
        link_id_(link_id),
        result_(result) {
    page_->GetSnapshot(
        page_snapshot_.NewRequest(), [this](ledger::Status status) {
          page_snapshot_->Get(
              to_array(link_id_),
              [this](ledger::Status status, ledger::ValuePtr value) {
                if (value) {
                  data_ = LinkData::New();
                  data_->Deserialize(value->get_bytes().data(),
                                     value->get_bytes().size());
                }
                result_(std::move(data_));
                Done();
              });
        });
  }

 private:
  ledger::Page* const page_;  // not owned
  ledger::PageSnapshotPtr page_snapshot_;
  const fidl::String link_id_;
  LinkDataPtr data_;
  Result result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadLinkDataCall);
};

class WriteLinkDataCall : public Transaction {
 public:
  using Result = StoryStorageImpl::WriteLinkDataCallback;

  WriteLinkDataCall(TransactionContainer* const container,
                    ledger::Page* const page,
                    const fidl::String& link_id,
                    LinkDataPtr data,
                    Result result)
      : Transaction(container),
        page_(page),
        link_id_(link_id),
        data_(std::move(data)),
        result_(result) {
    fidl::Array<uint8_t> bytes;
    bytes.resize(data_->GetSerializedSize());
    data_->Serialize(bytes.data(), bytes.size());

    page_->Put(to_array(link_id_), std::move(bytes),
               [this](ledger::Status status) {
                 result_();
                 Done();
               });
  }

 private:
  ledger::Page* const page_;  // not owned
  ledger::PageSnapshotPtr page_snapshot_;
  const fidl::String link_id_;
  LinkDataPtr data_;
  Result result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(WriteLinkDataCall);
};

}  // namespace

StoryStorageImpl::StoryStorageImpl(std::shared_ptr<Storage> storage,
                                   ledger::PagePtr story_page,
                                   const fidl::String& key,
                                   fidl::InterfaceRequest<StoryStorage> request)
    : page_watcher_binding_(this),
      key_(key),
      storage_(storage),
      // Comment out this initializer in order to switch to in-memory storage.
      story_page_(std::move(story_page)) {
  bindings_.AddBinding(this, std::move(request));

  fidl::InterfaceHandle<ledger::PageWatcher> watcher;
  page_watcher_binding_.Bind(watcher.NewRequest());
  if (story_page_.is_bound()) {
    story_page_->Watch(std::move(watcher), [](ledger::Status status) {});
  }
}

// |StoryStorage|
void StoryStorageImpl::ReadLinkData(const fidl::String& link_id,
                                    const ReadLinkDataCallback& cb) {
  if (story_page_.is_bound()) {
    new ReadLinkDataCall(&transaction_container_, story_page_.get(), link_id,
                         cb);

  } else {
    auto& story_data = (*storage_)[key_];
    auto i = story_data.find(link_id);
    if (i != story_data.end()) {
      cb(i->second->Clone());
    } else {
      cb(nullptr);
    }
  }
}

// |StoryStorage|
void StoryStorageImpl::WriteLinkData(const fidl::String& link_id,
                                     LinkDataPtr data,
                                     const WriteLinkDataCallback& cb) {
  if (story_page_.is_bound()) {
    new WriteLinkDataCall(&transaction_container_, story_page_.get(), link_id,
                          std::move(data), cb);

  } else {
    (*storage_)[key_][link_id] = std::move(data);
    cb();
  }
}

// |StoryStorage|
void StoryStorageImpl::WatchLink(
    const fidl::String& link_id,
    fidl::InterfaceHandle<StoryStorageLinkWatcher> watcher) {
  watchers_.emplace_back(std::make_pair(
      link_id, StoryStorageLinkWatcherPtr::Create(std::move(watcher))));
}

// |StoryStorage|
void StoryStorageImpl::Dup(fidl::InterfaceRequest<StoryStorage> dup) {
  bindings_.AddBinding(this, std::move(dup));
}

// |PageWatcher|
void StoryStorageImpl::OnInitialState(
    fidl::InterfaceHandle<ledger::PageSnapshot> page,
    const OnInitialStateCallback& cb) {
  // TODO(mesch): We get the initial state from a direct query. This
  // leaves the possibility that the next OnChange is against a
  // different base state.
  cb();
}

// |PageWatcher|
void StoryStorageImpl::OnChange(ledger::PageChangePtr page,
                                const OnChangeCallback& cb) {
  if (!page.is_null() && !page->changes.is_null()) {
    for (auto& entry : page->changes) {
      const fidl::String link_id = to_string(entry->key);
      for (auto& watcher_entry : watchers_) {
        if (link_id == watcher_entry.first) {
          auto data = LinkData::New();
          data->Deserialize(entry->new_value->get_bytes().data(),
                            entry->new_value->get_bytes().size());
          watcher_entry.second->OnChange(std::move(data));
        }
      }
    }
  }
  cb(nullptr);
}

}  // namespace modular
