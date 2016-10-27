// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/abax/page_connector.h"

#include <utility>

#include "lib/ftl/logging.h"

namespace ledger {

PageConnector::PageConnector(mojo::InterfaceRequest<Page> request,
                             PageImpl* page)
    : page_(page), binding_(this, std::move(request)) {
  // Set up the error handler.
  binding_.set_connection_error_handler(
      [this]() { page_->OnConnectorError(this); });
}

PageConnector::~PageConnector() {}

// GetId() => (array<uint8> id);
void PageConnector::GetId(const GetIdCallback& callback) {
  callback.Run(page_->GetId());
}

// GetSnapshot() => (Status status, PageSnapshot? snapshot);
void PageConnector::GetSnapshot(const GetSnapshotCallback& callback) {
  callback.Run(Status::OK, page_->GetSnapshot());
}

// Watch(PageWatcher watcher) => (Status status);
void PageConnector::Watch(mojo::InterfaceHandle<PageWatcher> watcher,
                          const WatchCallback& callback) {
  callback.Run(page_->Watch(std::move(watcher)));
}

// Put(array<uint8> key, array<uint8> value) => (Status status);
void PageConnector::Put(mojo::Array<uint8_t> key,
                        mojo::Array<uint8_t> value,
                        const PutCallback& callback) {
  PutWithPriority(std::move(key), std::move(value), Priority::EAGER, callback);
}

// PutWithPriority(array<uint8> key, array<uint8> value, Priority priority)
//   => (Status status);
void PageConnector::PutWithPriority(mojo::Array<uint8_t> key,
                                    mojo::Array<uint8_t> value,
                                    Priority priority,
                                    const PutWithPriorityCallback& callback) {
  callback.Run(
      page_->Put(std::move(key), std::move(value), ChangeSource::LOCAL));
}

// PutReference(array<uint8> key, Reference? reference, Priority priority)
//   => (Status status);
void PageConnector::PutReference(mojo::Array<uint8_t> key,
                                 ReferencePtr reference,
                                 Priority priority,
                                 const PutReferenceCallback& callback) {
  callback.Run(page_->PutReference(std::move(key), std::move(reference)));
}

// Delete(array<uint8> key) => (Status status);
void PageConnector::Delete(mojo::Array<uint8_t> key,
                           const DeleteCallback& callback) {
  callback.Run(page_->Delete(std::move(key), ChangeSource::LOCAL));
}

// CreateReference(int64 size, handle<data_pipe_producer> buffer)
//   => (Status status, Reference reference);
void PageConnector::CreateReference(int64_t size,
                                    mojo::ScopedDataPipeConsumerHandle data,
                                    const CreateReferenceCallback& callback) {
  page_->CreateReference(size, std::move(data),
                         [callback](Status status, ReferencePtr reference) {
                           callback.Run(status, std::move(reference));
                         });
}

// GetReference(Reference reference) => (Status status, Value? value);
void PageConnector::GetReference(ReferencePtr reference,
                                 const GetReferenceCallback& callback) {
  ValuePtr value;
  Status status = page_->GetReference(std::move(reference), &value);
  callback.Run(status, std::move(value));
}

// GetPartialReference(Reference reference, int64 offset, int64 max_size)
//   => (Status status, handle<shared_buffer>? buffer);
void PageConnector::GetPartialReference(
    ReferencePtr reference,
    int64_t offset,
    int64_t max_size,
    const GetPartialReferenceCallback& callback) {
  mojo::ScopedSharedBufferHandle buffer;
  Status status = page_->GetPartialReference(std::move(reference), offset,
                                             max_size, &buffer);
  callback.Run(status, std::move(buffer));
}

// StartTransaction() => (Status status);
void PageConnector::StartTransaction(const StartTransactionCallback& callback) {
  FTL_LOG(ERROR) << "PageConnector::StartTransaction not implemented.";
  callback.Run(Status::UNKNOWN_ERROR);
}

// Commit() => (Status status);
void PageConnector::Commit(const CommitCallback& callback) {
  FTL_LOG(ERROR) << "PageConnector::Commit not implemented.";
  callback.Run(Status::UNKNOWN_ERROR);
}

// Rollback() => (Status status);
void PageConnector::Rollback(const RollbackCallback& callback) {
  FTL_LOG(ERROR) << "PageConnector::Rollback not implemented.";
  callback.Run(Status::UNKNOWN_ERROR);
}

}  // namespace ledger
