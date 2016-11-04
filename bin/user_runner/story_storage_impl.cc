// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/user_runner/story_storage_impl.h"

#include "apps/ledger/services/ledger.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/src/user_runner/transaction.h"

namespace modular {

namespace {

class ReadStoryDataCall : public Transaction {
 public:
  using Result = StoryStorageImpl::ReadStoryDataCallback;

  ReadStoryDataCall(TransactionContainer* const container,
                      ledger::Page* const page,
                      Result result) :
      Transaction(container),
      page_(page),
      result_(result) {
    page_->GetSnapshot(
        GetProxy(&page_snapshot_),
        [this](ledger::Status status) {
          page_snapshot_->Get(
              to_array("story_data"),
              [this](ledger::Status status, ledger::ValuePtr value) {
                if (value) {
                  data_ = StoryData::New();
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
  StoryDataPtr data_;
  Result result_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReadStoryDataCall);
};

}  // namespace

StoryStorageImpl::StoryStorageImpl(std::shared_ptr<Storage> storage,
                                   ledger::PagePtr story_page,
                                   const fidl::String& key,
                                   fidl::InterfaceRequest<StoryStorage> request)
    : binding_(this, std::move(request)), key_(key), storage_(storage),
      story_page_(std::move(story_page)) {
  FTL_LOG(INFO) << "StoryStorageImpl() " << this << " " << key_;
}

StoryStorageImpl::~StoryStorageImpl() {
  FTL_LOG(INFO) << "~StoryStorageImpl() " << this << " " << key_;
}

void StoryStorageImpl::ReadStoryData(const ReadStoryDataCallback& cb) {
  FTL_LOG(INFO) << "StoryStorageImpl::ReadStoryData() " << this << " " << key_;

  if (story_page_.is_bound()) {
    new ReadStoryDataCall(
        &transaction_container_,
        story_page_.get(),
        cb);

  } else {
    if (storage_->find(key_) != storage_->end()) {
      cb(storage_->find(key_)->second->Clone());
    } else {
      cb(nullptr);
    }
  }
}

void StoryStorageImpl::WriteStoryData(StoryDataPtr data) {
  FTL_LOG(INFO) << "StoryStorageImpl::WriteStoryData() " << this << " " << key_;
  if (story_page_.is_bound()) {
    fidl::Array<uint8_t> bytes;
    bytes.resize(data->GetSerializedSize());
    data->Serialize(bytes.data(), bytes.size());
    // Destructor is called synchronously by client after this method
    // returns, so the pipe closes before the result callback is
    // received.
    story_page_->Put(to_array("story_data"), std::move(bytes),
                       ledger::Page::PutCallback());

  } else {
    storage_->emplace(std::make_pair(key_, std::move(data)));
  }
}

}  // namespace modular
