// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_IMPL_H_
#define SRC_LEDGER_BIN_APP_PAGE_IMPL_H_

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>

#include "fuchsia/ledger/cpp/fidl.h"
#include "src/ledger/bin/app/delaying_facade.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/callback/scoped_task_runner.h"

namespace ledger {
class PageDelegate;

// An implementation of the |Page| FIDL interface.
class PageImpl : public fuchsia::ledger::PageSyncableDelegate {
 public:
  explicit PageImpl(async_dispatcher_t* dispatcher, storage::PageIdView page_id,
                    fidl::InterfaceRequest<Page> request);
  PageImpl(const PageImpl&) = delete;
  PageImpl& operator=(const PageImpl&) = delete;
  ~PageImpl() override;

  void SetPageDelegate(PageDelegate* page_delegate);

  bool IsDiscardable() const;

  void set_on_binding_unbound(fit::closure on_binding_unbound_callback);

 private:
  // Page:
  void GetId(fit::function<void(Status, PageId)> callback) override;
  void GetSnapshot(fidl::InterfaceRequest<PageSnapshot> snapshot_request,
                   std::vector<uint8_t> key_prefix, fidl::InterfaceHandle<PageWatcher> watcher,
                   fit::function<void(Status)> callback) override;
  void Put(std::vector<uint8_t> key, std::vector<uint8_t> value,
           fit::function<void(Status)> callback) override;
  void PutWithPriority(std::vector<uint8_t> key, std::vector<uint8_t> value, Priority priority,
                       fit::function<void(Status)> callback) override;
  void PutReference(std::vector<uint8_t> key, Reference reference, Priority priority,
                    fit::function<void(Status)> callback) override;
  void Delete(std::vector<uint8_t> key, fit::function<void(Status)> callback) override;
  void Clear(fit::function<void(Status)> callback) override;
  void CreateReferenceFromSocket(
      uint64_t size, zx::socket data,
      fit::function<void(Status, fuchsia::ledger::Page_CreateReferenceFromSocket_Result)> callback)
      override;
  void CreateReferenceFromBuffer(
      fuchsia::mem::Buffer data,
      fit::function<void(Status, fuchsia::ledger::Page_CreateReferenceFromBuffer_Result)> callback)
      override;
  void StartTransaction(fit::function<void(Status)> callback) override;
  void Commit(fit::function<void(Status)> callback) override;
  void Rollback(fit::function<void(Status)> callback) override;
  void SetSyncStateWatcher(fidl::InterfaceHandle<SyncWatcher> watcher,
                           fit::function<void(Status)> callback) override;
  void WaitForConflictResolution(
      fit::function<void(Status, ConflictResolutionWaitStatus)> callback) override;

  PageId page_id_;
  DelayingFacade<PageDelegate> delaying_facade_;

  fit::closure on_binding_unbound_callback_;

  SyncableBinding<fuchsia::ledger::PageSyncableDelegate> binding_;

  // Must be the last member field.
  ScopedTaskRunner task_runner_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_IMPL_H_
