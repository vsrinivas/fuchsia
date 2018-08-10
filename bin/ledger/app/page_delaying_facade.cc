// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/app/page_delaying_facade.h"

#include "peridot/lib/convert/convert.h"

namespace ledger {

PageDelayingFacade::PageDelayingFacade(storage::PageIdView page_id,
                                       fidl::InterfaceRequest<Page> request)
    : interface_(this) {
  convert::ToArray(page_id, &page_id_.id);

  interface_.set_on_empty([this] {
    if (on_empty_callback_) {
      on_empty_callback_();
    }
  });
  interface_.Bind(std::move(request));
}

void PageDelayingFacade::SetPageDelegate(PageDelegate* page_delegate) {
  delaying_facade_.SetTargetObject(page_delegate);
}

bool PageDelayingFacade::IsEmpty() { return !interface_.is_bound(); }

void PageDelayingFacade::GetId(Page::GetIdCallback callback) {
  callback(page_id_);
}

void PageDelayingFacade::GetSnapshot(
    fidl::InterfaceRequest<PageSnapshot> snapshot_request,
    fidl::VectorPtr<uint8_t> key_prefix,
    fidl::InterfaceHandle<PageWatcher> watcher,
    Page::GetSnapshotCallback callback) {
  delaying_facade_.EnqueueCall(
      &PageDelegate::GetSnapshot, std::move(snapshot_request),
      std::move(key_prefix), std::move(watcher), std::move(callback));
}

void PageDelayingFacade::Put(fidl::VectorPtr<uint8_t> key,
                             fidl::VectorPtr<uint8_t> value,
                             Page::PutCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::Put, std::move(key),
                               std::move(value), std::move(callback));
}

void PageDelayingFacade::PutWithPriority(
    fidl::VectorPtr<uint8_t> key, fidl::VectorPtr<uint8_t> value,
    Priority priority, Page::PutWithPriorityCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::PutWithPriority, std::move(key),
                               std::move(value), priority, std::move(callback));
}

void PageDelayingFacade::PutReference(fidl::VectorPtr<uint8_t> key,
                                      Reference reference, Priority priority,
                                      Page::PutReferenceCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::PutReference, std::move(key),
                               std::move(reference), priority,
                               std::move(callback));
}

void PageDelayingFacade::Delete(fidl::VectorPtr<uint8_t> key,
                                Page::DeleteCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::Delete, std::move(key),
                               std::move(callback));
}

void PageDelayingFacade::Clear(Page::ClearCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::Clear, std::move(callback));
}

void PageDelayingFacade::CreateReference(
    std::unique_ptr<storage::DataSource> data,
    fit::function<void(Status, ReferencePtr)> callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::CreateReference, std::move(data),
                               std::move(callback));
}

void PageDelayingFacade::StartTransaction(
    Page::StartTransactionCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::StartTransaction,
                               std::move(callback));
}

void PageDelayingFacade::Commit(Page::CommitCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::Commit, std::move(callback));
}

void PageDelayingFacade::Rollback(Page::RollbackCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::Rollback, std::move(callback));
}

void PageDelayingFacade::SetSyncStateWatcher(
    fidl::InterfaceHandle<SyncWatcher> watcher,
    Page::SetSyncStateWatcherCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::SetSyncStateWatcher,
                               std::move(watcher), std::move(callback));
}

void PageDelayingFacade::WaitForConflictResolution(
    Page::WaitForConflictResolutionCallback callback) {
  delaying_facade_.EnqueueCall(&PageDelegate::WaitForConflictResolution,
                               std::move(callback));
}

}  // namespace ledger
