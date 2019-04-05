// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_IMPL_H_
#define SRC_LEDGER_BIN_APP_PAGE_IMPL_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/macros.h>

#include "src/ledger/bin/app/delaying_facade.h"
#include "src/ledger/bin/fidl/error_notifier.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {
class PageDelegate;

// An implementation of the |Page| FIDL interface.
class PageImpl : public fuchsia::ledger::PageErrorNotifierDelegate {
 public:
  explicit PageImpl(storage::PageIdView page_id,
                    fidl::InterfaceRequest<Page> request);
  ~PageImpl() override;

  void SetPageDelegate(PageDelegate* page_delegate);

  bool IsEmpty();

  void set_on_binding_unbound(fit::closure on_binding_unbound_callback);

 private:
  // Page:
  void GetId(fit::function<void(Status, PageId)> callback) override;
  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   std::vector<uint8_t> key_prefix,
                   fidl::InterfaceHandle<PageWatcher> watcher,
                   fit::function<void(Status)> callback) override;
  void GetSnapshotNew(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                      std::vector<uint8_t> key_prefix,
                      fidl::InterfaceHandle<PageWatcher> watcher,
                      fit::function<void(Status)> callback) override;
  void Put(std::vector<uint8_t> key, std::vector<uint8_t> value,
           fit::function<void(Status)> callback) override;
  void PutNew(std::vector<uint8_t> key, std::vector<uint8_t> value,
              fit::function<void(Status)> callback) override;
  void PutWithPriority(std::vector<uint8_t> key, std::vector<uint8_t> value,
                       Priority priority,
                       fit::function<void(Status)> callback) override;
  void PutWithPriorityNew(std::vector<uint8_t> key, std::vector<uint8_t> value,
                          Priority priority,
                          fit::function<void(Status)> callback) override;
  void PutReference(std::vector<uint8_t> key, Reference reference,
                    Priority priority,
                    fit::function<void(Status)> callback) override;
  void PutReferenceNew(std::vector<uint8_t> key, Reference reference,
                       Priority priority,
                       fit::function<void(Status)> callback) override;
  void Delete(std::vector<uint8_t> key,
              fit::function<void(Status)> callback) override;
  void DeleteNew(std::vector<uint8_t> key,
                 fit::function<void(Status)> callback) override;
  void Clear(fit::function<void(Status)> callback) override;
  void ClearNew(fit::function<void(Status)> callback) override;
  void CreateReferenceFromSocket(
      uint64_t size, zx::socket data,
      fit::function<void(Status, CreateReferenceStatus,
                         std::unique_ptr<Reference>)>
          callback) override;
  void CreateReferenceFromSocketNew(
      uint64_t size, zx::socket data,
      fit::function<void(Status, CreateReferenceStatus,
                         std::unique_ptr<Reference>)>
          callback) override;
  void CreateReferenceFromBuffer(
      fuchsia::mem::Buffer data,
      fit::function<void(Status, CreateReferenceStatus,
                         std::unique_ptr<Reference>)>
          callback) override;
  void CreateReferenceFromBufferNew(
      fuchsia::mem::Buffer data,
      fit::function<void(Status, CreateReferenceStatus,
                         std::unique_ptr<Reference>)>
          callback) override;
  void StartTransaction(fit::function<void(Status)> callback) override;
  void StartTransactionNew(fit::function<void(Status)> callback) override;
  void Commit(fit::function<void(Status)> callback) override;
  void CommitNew(fit::function<void(Status)> callback) override;
  void Rollback(fit::function<void(Status)> callback) override;
  void RollbackNew(fit::function<void(Status)> callback) override;
  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           fit::function<void(Status)> callback) override;
  void SetSyncStateWatcherNew(fidl::InterfaceHandle<SyncWatcher> watcher,
                              fit::function<void(Status)> callback) override;
  void WaitForConflictResolution(
      fit::function<void(Status, ConflictResolutionWaitStatus)> callback)
      override;

  PageId page_id_;
  DelayingFacade<PageDelegate> delaying_facade_;

  fit::closure on_binding_unbound_callback_;

  ErrorNotifierBinding<fuchsia::ledger::PageErrorNotifierDelegate> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageImpl);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_IMPL_H_
