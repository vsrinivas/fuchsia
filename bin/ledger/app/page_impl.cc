// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_impl.h"

#include <lib/callback/trace_callback.h>
#include <lib/fxl/logging.h>
#include <trace/event.h>

#include "peridot/bin/ledger/app/page_delegate.h"

namespace ledger {

PageImpl::PageImpl(PageDelegate* delegate) : delegate_(delegate) {}

PageImpl::~PageImpl() {}

void PageImpl::GetId(GetIdCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_get_id");
  delegate_->GetId(std::move(timed_callback));
}

void PageImpl::GetSnapshot(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    fidl::VectorPtr<uint8_t> key_prefix,
    fidl::InterfaceHandle<PageWatcher> watcher, GetSnapshotCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_get_snapshot");
  delegate_->GetSnapshot(std::move(snapshot_request), std::move(key_prefix),
                         std::move(watcher), std::move(timed_callback));
}

void PageImpl::Put(fidl::VectorPtr<uint8_t> key, fidl::VectorPtr<uint8_t> value,
                   PutCallback callback) {
  PutWithPriority(std::move(key), std::move(value), Priority::EAGER,
                  std::move(callback));
}

void PageImpl::PutWithPriority(fidl::VectorPtr<uint8_t> key,
                               fidl::VectorPtr<uint8_t> value,
                               Priority priority,
                               PutWithPriorityCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_put_with_priority");
  delegate_->PutWithPriority(std::move(key), std::move(value), priority,
                             std::move(timed_callback));
}

void PageImpl::PutReference(fidl::VectorPtr<uint8_t> key, Reference reference,
                            Priority priority, PutReferenceCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_put_reference");
  delegate_->PutReference(std::move(key), std::move(reference), priority,
                          std::move(timed_callback));
}

void PageImpl::Delete(fidl::VectorPtr<uint8_t> key, DeleteCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_delete");
  delegate_->Delete(std::move(key), std::move(timed_callback));
}

void PageImpl::Clear(ClearCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_clear");
  delegate_->Clear(std::move(timed_callback));
}

void PageImpl::CreateReferenceFromSocket(
    uint64_t size, zx::socket data,
    CreateReferenceFromSocketCallback callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger",
                                       "page_create_reference_from_socket");
  delegate_->CreateReference(storage::DataSource::Create(std::move(data), size),
                             std::move(timed_callback));
}

void PageImpl::CreateReferenceFromBuffer(
    fuchsia::mem::Buffer data, CreateReferenceFromBufferCallback callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger",
                                       "page_create_reference_from_vmo");
  fsl::SizedVmo vmo;
  if (!fsl::SizedVmo::FromTransport(std::move(data), &vmo)) {
    callback(Status::INVALID_ARGUMENT, nullptr);
    return;
  }
  delegate_->CreateReference(storage::DataSource::Create(std::move(vmo)),
                             std::move(timed_callback));
}

void PageImpl::StartTransaction(StartTransactionCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_start_transaction");
  delegate_->StartTransaction(std::move(timed_callback));
}

void PageImpl::Commit(CommitCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_commit");
  delegate_->Commit(std::move(timed_callback));
}

void PageImpl::Rollback(RollbackCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_rollback");
  delegate_->Rollback(std::move(timed_callback));
}

void PageImpl::SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                                   SetSyncStateWatcherCallback callback) {
  delegate_->SetSyncStateWatcher(std::move(watcher), std::move(callback));
}

void PageImpl::WaitForConflictResolution(
    WaitForConflictResolutionCallback callback) {
  delegate_->WaitForConflictResolution(std::move(callback));
}

}  // namespace ledger
