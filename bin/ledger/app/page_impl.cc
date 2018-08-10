// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_impl.h"

#include <lib/callback/trace_callback.h>
#include <lib/fxl/logging.h>
#include <trace/event.h>

#include "peridot/bin/ledger/app/page_delaying_facade.h"

namespace ledger {

PageImpl::PageImpl(PageDelayingFacade* delaying_facade)
    : delaying_facade_(delaying_facade) {}

PageImpl::~PageImpl() {}

void PageImpl::GetId(GetIdCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_get_id");
  delaying_facade_->GetId(std::move(timed_callback));
}

void PageImpl::GetSnapshot(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    fidl::VectorPtr<uint8_t> key_prefix,
    fidl::InterfaceHandle<PageWatcher> watcher, GetSnapshotCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_get_snapshot");
  delaying_facade_->GetSnapshot(std::move(snapshot_request),
                                std::move(key_prefix), std::move(watcher),
                                std::move(timed_callback));
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
  delaying_facade_->PutWithPriority(std::move(key), std::move(value), priority,
                                    std::move(timed_callback));
}

void PageImpl::PutReference(fidl::VectorPtr<uint8_t> key, Reference reference,
                            Priority priority, PutReferenceCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_put_reference");
  delaying_facade_->PutReference(std::move(key), std::move(reference), priority,
                                 std::move(timed_callback));
}

void PageImpl::Delete(fidl::VectorPtr<uint8_t> key, DeleteCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_delete");
  delaying_facade_->Delete(std::move(key), std::move(timed_callback));
}

void PageImpl::Clear(ClearCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_clear");
  delaying_facade_->Clear(std::move(timed_callback));
}

void PageImpl::CreateReferenceFromSocket(
    uint64_t size, zx::socket data,
    CreateReferenceFromSocketCallback callback) {
  auto timed_callback = TRACE_CALLBACK(std::move(callback), "ledger",
                                       "page_create_reference_from_socket");
  delaying_facade_->CreateReference(
      storage::DataSource::Create(std::move(data), size),
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
  delaying_facade_->CreateReference(storage::DataSource::Create(std::move(vmo)),
                                    std::move(timed_callback));
}

void PageImpl::StartTransaction(StartTransactionCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_start_transaction");
  delaying_facade_->StartTransaction(std::move(timed_callback));
}

void PageImpl::Commit(CommitCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_commit");
  delaying_facade_->Commit(std::move(timed_callback));
}

void PageImpl::Rollback(RollbackCallback callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_rollback");
  delaying_facade_->Rollback(std::move(timed_callback));
}

void PageImpl::SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                                   SetSyncStateWatcherCallback callback) {
  delaying_facade_->SetSyncStateWatcher(std::move(watcher),
                                        std::move(callback));
}

void PageImpl::WaitForConflictResolution(
    WaitForConflictResolutionCallback callback) {
  delaying_facade_->WaitForConflictResolution(std::move(callback));
}

}  // namespace ledger
