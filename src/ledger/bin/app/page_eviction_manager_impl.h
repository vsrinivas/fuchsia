// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_IMPL_H_
#define SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_IMPL_H_

#include <lib/fit/function.h>

#include <memory>
#include <utility>

#include "src/ledger/bin/app/page_eviction_manager.h"
#include "src/ledger/bin/app/page_usage_db.h"
#include "src/ledger/bin/app/page_utils.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/public/db_factory.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/ledger/lib/coroutine/coroutine_manager.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace ledger {

class PageEvictionManagerImpl : public PageEvictionManager,
                                public PageEvictionDelegate {
 public:
  PageEvictionManagerImpl(Environment* environment,
                          storage::DbFactory* db_factory, DetachedPath db_path);
  ~PageEvictionManagerImpl() override;

  // Initializes this PageEvictionManager. |IO_ERROR| will be returned in case
  // of an error while initializing the underlying database.
  storage::Status Init();

  // Sets the delegate for this PageEvictionManagerImpl. The delegate should
  // outlive this object.
  void SetDelegate(PageEvictionManager::Delegate* delegate);

  // PageEvictionManager:
  void set_on_empty(fit::closure on_empty_callback) override;

  bool IsEmpty() override;

  void TryEvictPages(PageEvictionPolicy* policy,
                     fit::function<void(storage::Status)> callback) override;

  void MarkPageOpened(fxl::StringView ledger_name,
                      storage::PageIdView page_id) override;

  void MarkPageClosed(fxl::StringView ledger_name,
                      storage::PageIdView page_id) override;

  // PageEvictionDelegate:
  void TryEvictPage(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      PageEvictionCondition condition,
      fit::function<void(storage::Status, PageWasEvicted)> callback) override;

 private:
  // A Completer allowing waiting until the target operation is completed.
  class Completer {
   public:
    Completer();

    ~Completer();

    // Completes the operation with the given status and unblocks all pending
    // |WaitUntilDone| calls. |Complete| can only be called once.
    void Complete(storage::Status status);

    // Blocks execution until |Complete| is called, and then returns its status.
    // If the operation is already completed, |WaitUntilDone| returns
    // immediately with the result status.
    storage::Status WaitUntilDone(coroutine::CoroutineHandler* handler);

   private:
    // Marks the Completer as completed with the given status and calls the
    // pending callbacks.
    void CallCallbacks(storage::Status status);

    bool completed_ = false;
    storage::Status status_;
    // Closures invoked upon completion to unblock the waiting coroutines.
    std::vector<fit::closure> callbacks_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Completer);
  };

  // Removes the page from the local storage. The caller of this method must
  // ensure that the given page exists.
  void EvictPage(fxl::StringView ledger_name, storage::PageIdView page_id,
                 fit::function<void(storage::Status)> callback);

  // Checks whether a page can be evicted. A page can be evicted if it is
  // currently closed and either:
  // - has no unsynced commits or objects, or
  // - is empty and offline, i.e. was never synced to the cloud or a peer.
  storage::Status CanEvictPage(coroutine::CoroutineHandler* handler,
                               fxl::StringView ledger_name,
                               storage::PageIdView page_id, bool* can_evict);

  // Checks whether a page is closed, offline and empty, and thus can be
  // evicted.
  storage::Status CanEvictEmptyPage(coroutine::CoroutineHandler* handler,
                                    fxl::StringView ledger_name,
                                    storage::PageIdView page_id,
                                    bool* can_evict);

  // Marks the given page as evicted in the page usage database.
  void MarkPageEvicted(std::string ledger_name, storage::PageId page_id);

  storage::Status SynchronousTryEvictPage(coroutine::CoroutineHandler* handler,
                                          std::string ledger_name,
                                          storage::PageId page_id,
                                          PageEvictionCondition condition,
                                          PageWasEvicted* was_evicted);

  ExpiringToken NewExpiringToken();

  Environment* environment_;
  // The initialization completer. |Init| method starts marking pages as closed,
  // and returns before that operation is done. This completer makes sure that
  // all methods accessing the page usage database wait until the initialization
  // has finished, before reading or updating information.
  Completer initialization_completer_;
  // A closure to be called every time all pending operations are completed.
  fit::closure on_empty_callback_;
  ssize_t pending_operations_ = 0;
  PageEvictionManager::Delegate* delegate_ = nullptr;
  // |db_factory_| and |db_path_| should only be used during initialization.
  // After Init() has been called their contents are no longer valid.
  storage::DbFactory* db_factory_;
  DetachedPath db_path_;
  std::unique_ptr<PageUsageDb> db_;
  coroutine::CoroutineManager coroutine_manager_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageEvictionManagerImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionManagerImpl);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_IMPL_H_
