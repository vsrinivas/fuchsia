// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_IMPL_H_

#include <lib/fxl/macros.h>

#include "peridot/bin/ledger/fidl/include/types.h"

namespace ledger {
class PageDelayingFacade;

// An implementation of the |Page| FIDL interface.
class PageImpl : public Page {
 public:
  explicit PageImpl(PageDelayingFacade* delaying_facade);
  ~PageImpl() override;

 private:
  // Page:
  void GetId(GetIdCallback callback) override;

  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   fidl::VectorPtr<uint8_t> key_prefix,
                   fidl::InterfaceHandle<PageWatcher> watcher,
                   GetSnapshotCallback callback) override;

  void Put(fidl::VectorPtr<uint8_t> key, fidl::VectorPtr<uint8_t> value,
           PutCallback callback) override;

  void PutWithPriority(fidl::VectorPtr<uint8_t> key,
                       fidl::VectorPtr<uint8_t> value, Priority priority,
                       PutWithPriorityCallback callback) override;

  void PutReference(fidl::VectorPtr<uint8_t> key, Reference reference,
                    Priority priority, PutReferenceCallback callback) override;

  void Delete(fidl::VectorPtr<uint8_t> key, DeleteCallback callback) override;

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

  PageDelayingFacade* delaying_facade_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_IMPL_H_
