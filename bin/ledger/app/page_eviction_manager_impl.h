// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_

#include "peridot/bin/ledger/app/page_eviction_manager.h"

#include <utility>

#include <lib/fit/function.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/bin/ledger/app/page_usage_db.h"
#include "peridot/bin/ledger/app/page_utils.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"
#include "peridot/bin/ledger/coroutine/coroutine_manager.h"

namespace ledger {

class PageEvictionManagerImpl : public PageEvictionManager {
 public:
  PageEvictionManagerImpl(async_dispatcher_t* dispatcher,
                          coroutine::CoroutineService* coroutine_service,
                          ledger::DetachedPath db_path);
  ~PageEvictionManagerImpl() override;

  // Initializes this PageEvictionManager. |IO_ERROR| will be returned in case
  // of an error while initializing the underlying database.
  Status Init();

  // Sets the delegate for this PageEvictionManagerImpl. The delegate should
  // outlive this object.
  void SetDelegate(PageEvictionManager::Delegate* delegate);

  // PageEvictionManager:
  void set_on_empty(fit::closure on_empty_callback) override;

  bool IsEmpty() override;

  void TryCleanUp(fit::function<void(Status)> callback) override;

  void OnPageOpened(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override;

  void OnPageClosed(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override;

 private:
  // A token that performs a given action on destruction. ExpiringToken objects
  // are used to keep track of pending operations.
  using ExpiringToken = fxl::AutoCall<fit::closure>;

  // A Completer allowing waiting until the target operation is completed.
  class Completer {
   public:
    Completer();

    ~Completer();

    // Completes the operation with the given status and unblocks all pending
    // |WaitUntilDone| calls. |Complete| can only be called once.
    void Complete(Status status);

    // Blocks execution until |Complete| is called, and then returns its status.
    // If the operation is already completed, |WaitUntilDone| returns
    // immediately with the result status.
    Status WaitUntilDone(coroutine::CoroutineHandler* handler);

   private:
    // Marks the Completer as completed with the given status and calls the
    // pending callbacks.
    void CallCallbacks(Status status);

    bool completed_ = false;
    Status status_;
    // Closures invoked upon completion to unblock the waiting coroutines.
    std::vector<fit::closure> callbacks_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Completer);
  };

  // Removes the page from the local storage.
  void EvictPage(fxl::StringView ledger_name, storage::PageIdView page_id,
                 fit::function<void(Status)> callback);

  // Checks whether a page can be evicted. We can evict pages that are not
  // currently used and have no unsynced commits or objects.
  Status CanEvictPage(coroutine::CoroutineHandler* handler,
                      fxl::StringView ledger_name, storage::PageIdView page_id,
                      bool* can_evict);

  // Computes the list of PageInfo for all pages that are not currently open,
  // ordered by the timestamp of their last usage, in ascending order.
  Status GetPagesByTimestamp(coroutine::CoroutineHandler* handler,
                             std::vector<PageUsageDb::PageInfo>* pages_info);

  // Marks the given page as evicted in the page usage database.
  void MarkPageEvicted(std::string ledger_name, storage::PageId page_id);

  ExpiringToken NewExpiringToken();

  async_dispatcher_t* dispatcher_;
  // The initialization completer. |Init| method starts marking pages as closed,
  // and returns before that operation is done. This completer makes sure that
  // all methods accessing the page usage database wait until the initialization
  // has finished, before reading or updating information.
  Completer initialization_completer_;
  // A closure to be called every time all pending operations are completed.
  fit::closure on_empty_callback_;
  ssize_t pending_operations_ = 0;
  PageEvictionManager::Delegate* delegate_ = nullptr;
  PageUsageDb db_;
  coroutine::CoroutineManager coroutine_manager_;

  // Must be the last member.
  fxl::WeakPtrFactory<PageEvictionManagerImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionManagerImpl);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_IMPL_H_
