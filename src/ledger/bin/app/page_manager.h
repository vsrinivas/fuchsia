// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_MANAGER_H_
#define SRC_LEDGER_BIN_APP_PAGE_MANAGER_H_

#include <lib/fidl/cpp/interface_request.h>
#include <lib/fit/function.h>

#include <optional>
#include <string>
#include <vector>

#include "src/ledger/bin/app/active_page_manager_container.h"
#include "src/ledger/bin/app/ledger_impl.h"
#include "src/ledger/bin/app/merging/ledger_merge_manager.h"
#include "src/ledger/bin/app/page_availability_manager.h"
#include "src/ledger/bin/app/page_impl.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/storage/public/ledger_storage.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/lib/memory/weak_ptr.h"

namespace ledger {
// Manages a ledger page.
//
// PageManager owns all page-level objects related to a single page: page
// storage, and a set of FIDL PageImpls backed by the page storage. It is safe
// to delete it at any point - this closes all channels, deletes PageImpls and
// tears down the storage.
//
// When a PageManager becomes empty, client is notified through
// |on_discardable|.
class PageManager {
 public:
  PageManager(Environment* environment, std::string ledger_name, storage::PageId page_id,
              std::vector<PageUsageListener*> page_usage_listeners,
              storage::LedgerStorage* ledger_storage, sync_coordinator::LedgerSync* ledger_sync,
              LedgerMergeManager* ledger_merge_manager);
  PageManager(const PageManager&) = delete;
  PageManager& operator=(const PageManager&) = delete;
  ~PageManager();

  // Checks whether the given page is closed and synced. The result returned in
  // the callback will be |PAGE_OPENED| if the page is opened after calling this
  // method and before the callback is called. Otherwise it will be |YES| or
  // |NO| depending on whether the page is synced or not.
  void PageIsClosedAndSynced(fit::function<void(storage::Status, PagePredicateResult)> callback);

  // Checks whether the given page is closed, offline and empty. The result
  // returned in the callback will be |PAGE_OPENED| if the page is opened after
  // calling this method and before the callback is called. Otherwise it will be
  // |YES| or |NO| depending on whether the page is offline and empty or not.
  void PageIsClosedOfflineAndEmpty(
      fit::function<void(storage::Status, PagePredicateResult)> callback);

  // Deletes the local copy of the page. If the page is currently open, the
  // callback will be called with |ILLEGAL_STATE|.
  void DeletePageStorage(fit::function<void(storage::Status)> callback);

  // Keeps track of |callback|. Binds |page| and fires |callback| when a
  // PageManager is available or an error occurs.
  void GetPage(LedgerImpl::Delegate::PageState page_state,
               fidl::InterfaceRequest<Page> page_request,
               fit::function<void(storage::Status)> callback);

  // Starts syncing the given page with the cloud if it is not currently open.
  void StartPageSync();

  // Registers "interest" in this |PageManager| for which this |PageManager|
  // will remain non-empty and returns a closure that when called will
  // deregister the "interest" in this |PageManager| (and potentially cause this
  // |PageManager|'s on_discardable_ to be called).
  fit::closure CreateDetacher();

  void SetOnDiscardable(fit::closure on_discardable);

  bool IsDiscardable() const;

 private:
  using PageTracker = fit::function<bool()>;

  // Requests a PageStorage object for the given |container|. If the page is not
  // locally available, the |callback| is called with |PAGE_NOT_FOUND|.
  void InitActivePageManagerContainer(ActivePageManagerContainer* container,
                                      fit::function<void(storage::Status)> callback);

  // Creates a page storage for the given |page_id| and completes the
  // ActivePageManagerContainer.
  void CreatePageStorage(LedgerImpl::Delegate::PageState page_state,
                         ActivePageManagerContainer* container);

  // Creates the |ActivePageManagerContainer| for this super manager.
  ActivePageManagerContainer* CreateActivePageManagerContainer();

  // Creates a new |ActivePageManager| for the given storage.
  void NewActivePageManager(
      std::unique_ptr<storage::PageStorage> page_storage, ActivePageManager::PageStorageState state,
      fit::function<void(storage::Status, std::unique_ptr<ActivePageManager>)> callback);

  // Checks whether the page is closed and satisfies the given |predicate|. The
  // |PagePredicateResult| passed to the given callback will be |PAGE_OPENED| if
  // the page is opened after calling this method and before the callback is
  // called. Otherwise it will be |YES| or |NO| depending on whether the
  // predicate is satisfied.
  void PageIsClosedAndSatisfiesPredicate(
      fit::function<void(ActivePageManager*, fit::function<void(storage::Status, bool)>)> predicate,
      fit::function<void(storage::Status, PagePredicateResult)> callback);

  // Returns a tracking Callable object for the page. When called, returns
  // |true| if the page has not been opened until now, and stops tracking the
  // page.
  PageTracker NewPageTracker();

  // If this page is among those whose usage is being tracked, marks this page
  // as opened. See also |was_opened_|.
  void MaybeMarkPageOpened();

  void CheckDiscardable();

  Environment* const environment_;
  const std::string ledger_name_;
  const storage::PageId page_id_;
  std::vector<PageUsageListener*> page_usage_listeners_;
  storage::LedgerStorage* ledger_storage_;
  sync_coordinator::LedgerSync* ledger_sync_;
  LedgerMergeManager* ledger_merge_manager_;

  fit::closure on_discardable_;

  PageAvailabilityManager page_availability_manager_;

  std::optional<ActivePageManagerContainer> active_page_manager_container_;

  // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=12303): Based on
  // the way was_opened_ is used there may be opportunities to simplify the
  // state that is maintained in the following fields.
  // |was_opened_| is used to track whether the page was opened during a given
  // operation. When |IsClosedAndSatisfiesPredicate| is called, a unique
  // operation id is added to this vector. The operation id will be deleted from
  // the vector either when that operation is done, or when the page is opened
  // because of an external request. This guarantees that if before calling the
  // callback of |PageIsClosedAndSatisfiesPredicate|, the operation id is still
  // present in the vector, the page was not opened during that operation.
  // Otherwise, the page was opened, and |PAGE_OPENED| should be returned.
  std::vector<uint64_t> was_opened_;
  uint64_t was_opened_id_ = 0;
  // |outstanding_operations_| counts the number of active tracking operations.
  // The super manager is not empty until all operations have completed.
  uint64_t outstanding_operations_ = 0;

  // A nonnegative count of the number of "registered interests" for this
  // |PageManager|. This field is incremented by calls to |CreateDetacher| and
  // decremented by calls to the closures returned by calls to |CreateDetacher|.
  // This |PageManager| is not considered empty while this number is positive.
  int64_t outstanding_detachers_ = 0;

  // Must be the last member.
  WeakPtrFactory<PageManager> weak_factory_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_MANAGER_H_
