// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_DELAYING_FACADE_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_DELAYING_FACADE_H_

#include "peridot/bin/ledger/app/delaying_facade.h"
#include "peridot/bin/ledger/app/page_delegate.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

// A handler for all calls to methods from the |Page| interface.
//
// |PageDelayingFacade| owns PageImpl. It makes sure that |Page::GetId| can be
// answered immediately after the page is bound, but also that all other methods
// are queued until the page initialization is complete.
//
// On |Page| request, a |PageDelayingFacade| should immediately be created and
// |BindPage| should be called. This will guarantee that |GetId| will get an
// immediate response and that the other method calls will be queued. Once page
// initialization is complete, |SetPageDelegate| should be called. After that,
// all pending operations, as well as any new ones, will be delegated to the
// given PageDelegate.
class PageDelayingFacade {
 public:
  // PageDelayingFacade constructor. The given request is bound immediately.
  PageDelayingFacade(storage::PageIdView page_id,
                     fidl::InterfaceRequest<Page> request);

  void SetPageDelegate(PageDelegate* page_delegate);

  bool IsEmpty();

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

  // From Page interface, called by PageImpl:
  void GetId(Page::GetIdCallback callback);

  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   fidl::VectorPtr<uint8_t> key_prefix,
                   fidl::InterfaceHandle<PageWatcher> watcher,
                   Page::GetSnapshotCallback callback);
  void Put(fidl::VectorPtr<uint8_t> key, fidl::VectorPtr<uint8_t> value,
           Page::PutCallback callback);
  void PutWithPriority(fidl::VectorPtr<uint8_t> key,
                       fidl::VectorPtr<uint8_t> value, Priority priority,
                       Page::PutWithPriorityCallback callback);
  void PutReference(fidl::VectorPtr<uint8_t> key, Reference reference,
                    Priority priority, Page::PutReferenceCallback callback);
  void Delete(fidl::VectorPtr<uint8_t> key, Page::DeleteCallback callback);
  void Clear(Page::ClearCallback callback);
  void CreateReference(std::unique_ptr<storage::DataSource> data,
                       fit::function<void(Status, ReferencePtr)> callback);
  void StartTransaction(Page::StartTransactionCallback callback);
  void Commit(Page::CommitCallback callback);
  void Rollback(Page::RollbackCallback callback);
  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           Page::SetSyncStateWatcherCallback callback);
  void WaitForConflictResolution(
      Page::WaitForConflictResolutionCallback callback);

 private:
  PageId page_id_;
  DelayingFacade<PageDelegate> delaying_facade_;

  fit::closure on_empty_callback_;

  fidl_helpers::BoundInterface<Page, PageImpl> interface_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageDelayingFacade);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_DELAYING_FACADE_H_
