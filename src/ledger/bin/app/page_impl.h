// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_IMPL_H_
#define SRC_LEDGER_BIN_APP_PAGE_IMPL_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>

#include "src/ledger/bin/app/delaying_facade.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {
class PageDelegate;

// An implementation of the |Page| FIDL interface.
class PageImpl : public Page {
 public:
  explicit PageImpl(storage::PageIdView page_id,
                    fidl::InterfaceRequest<Page> request);
  ~PageImpl() override;

  void SetPageDelegate(PageDelegate* page_delegate);

  bool IsEmpty();

  void set_on_binding_unbound(fit::closure on_binding_unbound_callback);

 private:
  // Page:
  void GetId(GetIdCallback callback) override;

  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   std::vector<uint8_t> key_prefix,
                   fidl::InterfaceHandle<PageWatcher> watcher,
                   GetSnapshotCallback callback) override;

  void Put(std::vector<uint8_t> key, std::vector<uint8_t> value,
           PutCallback callback) override;

  void PutWithPriority(std::vector<uint8_t> key, std::vector<uint8_t> value,
                       Priority priority,
                       PutWithPriorityCallback callback) override;

  void PutReference(std::vector<uint8_t> key, Reference reference,
                    Priority priority, PutReferenceCallback callback) override;

  void Delete(std::vector<uint8_t> key, DeleteCallback callback) override;

  void Clear(ClearCallback callback) override;

  void CreateReferenceFromSocket(
      uint64_t size, zx::socket data,
      CreateReferenceFromSocketCallback callback) override;

  void CreateReferenceFromBuffer(
      fuchsia::mem::Buffer data,
      CreateReferenceFromBufferCallback callback) override;

  void StartTransaction(StartTransactionCallback callback) override;

  void Commit(CommitCallback callback) override;

  void Rollback(RollbackCallback callback) override;

  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           SetSyncStateWatcherCallback callback) override;

  void WaitForConflictResolution(
      WaitForConflictResolutionCallback callback) override;

  PageId page_id_;
  DelayingFacade<PageDelegate> delaying_facade_;

  fit::closure on_binding_unbound_callback_;

  fidl::Binding<Page> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageImpl);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_IMPL_H_
