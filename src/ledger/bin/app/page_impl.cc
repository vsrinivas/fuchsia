// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/app/page_impl.h"

#include <lib/callback/trace_callback.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <trace/event.h>

#include "src/ledger/bin/app/page_delegate.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/lib/fxl/logging.h"

namespace ledger {
PageImpl::PageImpl(storage::PageIdView page_id,
                   fidl::InterfaceRequest<Page> request)
    : binding_(this) {
  convert::ToArray(page_id, &page_id_.id);
  binding_.set_on_empty([this] {
    binding_.Unbind();
    if (on_binding_unbound_callback_) {
      on_binding_unbound_callback_();
    }
  });
  binding_.Bind(std::move(request));
}

PageImpl::~PageImpl() {}

void PageImpl::SetPageDelegate(PageDelegate* page_delegate) {
  delaying_facade_.SetTargetObject(page_delegate);
}

bool PageImpl::IsEmpty() { return binding_.empty(); }

void PageImpl::set_on_binding_unbound(
    fit::closure on_binding_unbound_callback) {
  on_binding_unbound_callback_ = std::move(on_binding_unbound_callback);
}

void PageImpl::GetId(fit::function<void(Status, PageId)> callback) {
  auto timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_get_id");
  timed_callback(Status::OK, page_id_);
}

void PageImpl::GetSnapshot(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    std::vector<uint8_t> key_prefix, fidl::InterfaceHandle<PageWatcher> watcher,
    fit::function<void(Status)> callback) {
  fit::function<void(Status)> timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_get_snapshot");
  delaying_facade_.EnqueueCall(
      &PageDelegate::GetSnapshot, std::move(snapshot_request),
      std::move(key_prefix), std::move(watcher), std::move(timed_callback));
}

void PageImpl::GetSnapshotNew(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    std::vector<uint8_t> key_prefix, fidl::InterfaceHandle<PageWatcher> watcher,
    fit::function<void(Status)> callback) {
  PageImpl::GetSnapshot(std::move(snapshot_request), std::move(key_prefix),
                        std::move(watcher), std::move(callback));
}

void PageImpl::Put(std::vector<uint8_t> key, std::vector<uint8_t> value,
                   fit::function<void(Status)> callback) {
  PutWithPriority(std::move(key), std::move(value), Priority::EAGER,
                  std::move(callback));
}

void PageImpl::PutNew(std::vector<uint8_t> key, std::vector<uint8_t> value,
                      fit::function<void(Status)> callback) {
  Put(std::move(key), std::move(value), std::move(callback));
}

void PageImpl::PutWithPriority(std::vector<uint8_t> key,
                               std::vector<uint8_t> value, Priority priority,
                               fit::function<void(Status)> callback) {
  fit::function<void(Status)> timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_put_with_priority");
  delaying_facade_.EnqueueCall(&PageDelegate::PutWithPriority, std::move(key),
                               std::move(value), priority,
                               std::move(timed_callback));
}

void PageImpl::PutWithPriorityNew(std::vector<uint8_t> key,
                                  std::vector<uint8_t> value, Priority priority,
                                  fit::function<void(Status)> callback) {
  PageImpl::PutWithPriority(std::move(key), std::move(value), priority,
                            std::move(callback));
}

void PageImpl::PutReference(std::vector<uint8_t> key, Reference reference,
                            Priority priority,
                            fit::function<void(Status)> callback) {
  fit::function<void(Status)> timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_put_reference");
  delaying_facade_.EnqueueCall(&PageDelegate::PutReference, std::move(key),
                               std::move(reference), priority,
                               std::move(timed_callback));
}

void PageImpl::PutReferenceNew(std::vector<uint8_t> key, Reference reference,
                               Priority priority,
                               fit::function<void(Status)> callback) {
  PutReference(std::move(key), std::move(reference), priority,
               std::move(callback));
}

void PageImpl::Delete(std::vector<uint8_t> key,
                      fit::function<void(Status)> callback) {
  fit::function<void(Status)> timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_delete");
  delaying_facade_.EnqueueCall(&PageDelegate::Delete, std::move(key),
                               std::move(timed_callback));
}

void PageImpl::DeleteNew(std::vector<uint8_t> key,
                         fit::function<void(Status)> callback) {
  Delete(std::move(key), std::move(callback));
}

void PageImpl::Clear(fit::function<void(Status)> callback) {
  fit::function<void(Status)> timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_clear");
  delaying_facade_.EnqueueCall(&PageDelegate::Clear, std::move(timed_callback));
}

void PageImpl::ClearNew(fit::function<void(Status)> callback) {
  Clear(std::move(callback));
}

void PageImpl::CreateReferenceFromSocket(
    uint64_t size, zx::socket data,
    fit::function<void(Status, CreateReferenceStatus,
                       std::unique_ptr<Reference>)>
        callback) {
  fit::function<void(Status, CreateReferenceStatus, std::unique_ptr<Reference>)>
      timed_callback = TRACE_CALLBACK(std::move(callback), "ledger",
                                      "page_create_reference_from_socket");
  delaying_facade_.EnqueueCall(
      &PageDelegate::CreateReference,
      storage::DataSource::Create(std::move(data), size),
      std::move(timed_callback));
}

void PageImpl::CreateReferenceFromSocketNew(
    uint64_t size, zx::socket data,
    fit::function<void(Status, CreateReferenceStatus,
                       std::unique_ptr<Reference>)>
        callback) {
  CreateReferenceFromSocket(size, std::move(data), std::move(callback));
}

void PageImpl::CreateReferenceFromBuffer(
    fuchsia::mem::Buffer data, fit::function<void(Status, CreateReferenceStatus,
                                                  std::unique_ptr<Reference>)>
                                   callback) {
  fit::function<void(Status, CreateReferenceStatus, std::unique_ptr<Reference>)>
      timed_callback = TRACE_CALLBACK(std::move(callback), "ledger",
                                      "page_create_reference_from_vmo");
  fsl::SizedVmo vmo;
  if (!fsl::SizedVmo::FromTransport(std::move(data), &vmo)) {
    callback(Status::OK, CreateReferenceStatus::INVALID_ARGUMENT, nullptr);
    return;
  }
  delaying_facade_.EnqueueCall(&PageDelegate::CreateReference,
                               storage::DataSource::Create(std::move(vmo)),
                               std::move(timed_callback));
}

void PageImpl::CreateReferenceFromBufferNew(
    fuchsia::mem::Buffer data, fit::function<void(Status, CreateReferenceStatus,
                                                  std::unique_ptr<Reference>)>
                                   callback) {
  CreateReferenceFromBuffer(std::move(data), std::move(callback));
}

void PageImpl::StartTransaction(fit::function<void(Status)> callback) {
  fit::function<void(Status)> timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_start_transaction");
  delaying_facade_.EnqueueCall(&PageDelegate::StartTransaction,
                               std::move(timed_callback));
}

void PageImpl::StartTransactionNew(fit::function<void(Status)> callback) {
  PageImpl::StartTransaction(std::move(callback));
}

void PageImpl::Commit(fit::function<void(Status)> callback) {
  fit::function<void(Status)> timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_commit");
  delaying_facade_.EnqueueCall(&PageDelegate::Commit,
                               std::move(timed_callback));
}

void PageImpl::CommitNew(fit::function<void(Status)> callback) {
  Commit(std::move(callback));
}

void PageImpl::Rollback(fit::function<void(Status)> callback) {
  fit::function<void(Status)> timed_callback =
      TRACE_CALLBACK(std::move(callback), "ledger", "page_rollback");
  delaying_facade_.EnqueueCall(&PageDelegate::Rollback,
                               std::move(timed_callback));
}

void PageImpl::RollbackNew(fit::function<void(Status)> callback) {
  Rollback(std::move(callback));
}

void PageImpl::SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                                   fit::function<void(Status)> callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::SetSyncStateWatcher,
                               std::move(watcher), std::move(callback));
}

void PageImpl::SetSyncStateWatcherNew(
    fidl::InterfaceHandle<SyncWatcher> watcher,
    fit::function<void(Status)> callback) {
  SetSyncStateWatcher(std::move(watcher), std::move(callback));
}

void PageImpl::WaitForConflictResolution(
    fit::function<void(Status, ConflictResolutionWaitStatus)> callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::WaitForConflictResolution,
                               std::move(callback));
}

}  // namespace ledger
