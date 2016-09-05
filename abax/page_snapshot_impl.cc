// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/abax/page_snapshot_impl.h"

#include <string>
#include <vector>

#include "lib/ftl/logging.h"

namespace ledger {

PageSnapshotImpl::PageSnapshotImpl(mojo::InterfaceRequest<PageSnapshot> request,
                                   std::map<std::string, std::string>* db,
                                   PageImpl* page, Serialization* serialization)
    : db_(*db),
      page_(page),
      serialization_(serialization),
      binding_(this, std::move(request)) {
  binding_.set_connection_error_handler(
      [this]() { page_->OnSnapshotError(this); });
}

PageSnapshotImpl::~PageSnapshotImpl() {}

// GetAll(array<uint8>? key_prefix) => (Status status, array<Entry>? entries);
void PageSnapshotImpl::GetAll(mojo::Array<uint8_t> key_prefix,
                              const GetAllCallback& callback) {
  mojo::Array<EntryPtr> array = mojo::Array<EntryPtr>::New(0);
  std::string prefix =
      serialization_->GetReferenceRowKey(std::move(key_prefix));
  auto end = serialization_->PrefixEnd(db_, prefix);
  for (auto it = db_.lower_bound(prefix); it != end; ++it) {
    EntryPtr entry = Entry::New();
    entry->key = serialization_->GetEntryKey(it->first);
    entry->value = convert::ToArray(db_.at(it->second));
    array.push_back(std::move(entry));
  }

  callback.Run(Status::OK, std::move(array));
}

// GetKeys(array<uint8>? key_prefix)
//     => (Status status, array<array<uint8>>? keys);
void PageSnapshotImpl::GetKeys(mojo::Array<uint8_t> key_prefix,
                               const GetKeysCallback& callback) {
  mojo::Array<mojo::Array<uint8_t>> array =
      mojo::Array<mojo::Array<uint8_t>>::New(0);

  std::string prefix =
      serialization_->GetReferenceRowKey(std::move(key_prefix));
  auto end = serialization_->PrefixEnd(db_, prefix);
  for (auto it = db_.lower_bound(prefix); it != end; ++it) {
    array.push_back(serialization_->GetEntryKey(it->first));
  }

  callback.Run(Status::OK, std::move(array));
}

// Get(array<uint8> key) => (Status status, array<uint8>? value);
void PageSnapshotImpl::Get(mojo::Array<uint8_t> key,
                           const GetCallback& callback) {
  std::map<std::string, std::string>::const_iterator reference_key =
      db_.find(serialization_->GetReferenceRowKey(key));
  if (reference_key == db_.end()) {
    callback.Run(Status::KEY_NOT_FOUND, nullptr);
    return;
  }

  ValuePtr value;
  Status status = page_->GetReferenceById(reference_key->second, &value);
  callback.Run(status, std::move(value));
}

// GetPartial(array<uint8> key, int64 offset, int64 max_size)
//   => (Status status, Stream? stream);
void PageSnapshotImpl::GetPartial(mojo::Array<uint8_t> key, int64_t offset,
                                  int64_t max_size,
                                  const GetPartialCallback& callback) {
  FTL_LOG(ERROR) << "PageSnapshotImpl::GetPartial not implemented.";
  callback.Run(Status::UNKNOWN_ERROR, nullptr);
}

}  // namespace ledger
