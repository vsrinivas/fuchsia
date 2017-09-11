// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_PAGE_IMPL_H_
#define APPS_LEDGER_SRC_APP_PAGE_IMPL_H_

#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/fxl/macros.h"

namespace ledger {
class PageDelegate;

// An implementation of the |Page| FIDL interface.
class PageImpl : public Page {
 public:
  explicit PageImpl(PageDelegate* delegate);
  ~PageImpl() override;

 private:
  // Page:
  void GetId(const GetIdCallback& callback) override;

  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   fidl::Array<uint8_t> key_prefix,
                   fidl::InterfaceHandle<PageWatcher> watcher,
                   const GetSnapshotCallback& callback) override;

  void Put(fidl::Array<uint8_t> key,
           fidl::Array<uint8_t> value,
           const PutCallback& callback) override;

  void PutWithPriority(fidl::Array<uint8_t> key,
                       fidl::Array<uint8_t> value,
                       Priority priority,
                       const PutWithPriorityCallback& callback) override;

  void PutReference(fidl::Array<uint8_t> key,
                    ReferencePtr reference,
                    Priority priority,
                    const PutReferenceCallback& callback) override;

  void Delete(fidl::Array<uint8_t> key,
              const DeleteCallback& callback) override;

  void CreateReferenceFromSocket(
      uint64_t size,
      mx::socket data,
      const CreateReferenceFromSocketCallback& callback) override;

  void CreateReferenceFromVmo(
      mx::vmo data,
      const CreateReferenceFromVmoCallback& callback) override;

  void StartTransaction(const StartTransactionCallback& callback) override;

  void Commit(const CommitCallback& callback) override;

  void Rollback(const RollbackCallback& callback) override;

  void SetSyncStateWatcher(
      fidl::InterfaceHandle<SyncWatcher> watcher,
      const SetSyncStateWatcherCallback& callback) override;

  PageDelegate* delegate_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageImpl);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_PAGE_IMPL_H_
