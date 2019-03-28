// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_LEDGER_MANAGER_H_
#define SRC_LEDGER_BIN_APP_LEDGER_MANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <type_traits>

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/callback/auto_cleanable.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/memory/weak_ptr.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/app/ledger_impl.h"
#include "src/ledger/bin/app/merging/ledger_merge_manager.h"
#include "src/ledger/bin/app/page_manager.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/error_notifier.h"
#include "src/ledger/bin/fidl/error_notifier/error_notifier_binding.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"

namespace ledger {

// Manages a ledger instance. A ledger instance represents the data scoped to a
// particular user and a particular client app.
//
// LedgerManager owns all per-ledger-instance objects: LedgerStorage and a FIDL
// LedgerImpl. It is safe to delete it at any point - this closes all channels,
// deletes the LedgerImpl and tears down the storage.
class LedgerManager : public LedgerImpl::Delegate {
 public:
  LedgerManager(
      Environment* environment, std::string ledger_name,
      std::unique_ptr<encryption::EncryptionService> encryption_service,
      std::unique_ptr<storage::LedgerStorage> storage,
      std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync,
      PageUsageListener* page_usage_listener);
  ~LedgerManager() override;

  // Creates a new proxy for the LedgerImpl managed by this LedgerManager.
  void BindLedger(fidl::InterfaceRequest<Ledger> ledger_request);

  // Checks whether the given page is closed and synced. The result returned in
  // the callback will be |PAGE_OPENED| if the page is opened after calling this
  // method and before the callback is called. Otherwise it will be |YES| or
  // |NO| depending on whether the page is synced or not.
  void PageIsClosedAndSynced(
      storage::PageIdView page_id,
      fit::function<void(storage::Status, PagePredicateResult)> callback);

  // Checks whether the given page is closed, offline and empty. The result
  // returned in the callback will be |PAGE_OPENED| if the page is opened after
  // calling this method and before the callback is called. Otherwise it will be
  // |YES| or |NO| depending on whether the page is offline and empty or not.
  void PageIsClosedOfflineAndEmpty(
      storage::PageIdView page_id,
      fit::function<void(storage::Status, PagePredicateResult)> callback);

  // Deletes the local copy of the page. If the page is currently open, the
  // callback will be called with |ILLEGAL_STATE|.
  void DeletePageStorage(convert::ExtendedStringView page_id,
                         fit::function<void(storage::Status)> callback);

  // LedgerImpl::Delegate:
  void GetPage(convert::ExtendedStringView page_id, PageState page_state,
               fidl::InterfaceRequest<Page> page_request,
               fit::function<void(storage::Status)> callback) override;
  void SetConflictResolverFactory(
      fidl::InterfaceHandle<ConflictResolverFactory> factory) override;

  void set_on_empty(fit::closure on_empty_callback) {
    on_empty_callback_ = std::move(on_empty_callback);
  }

 private:
  class PageManagerContainer;
  using PageTracker = fit::function<bool()>;

  // Stores whether a given page is busy or available. After |MarkPageBusy| has
  // been called, all calls to |OnPageAvailable| will be delayed until a call to
  // |MarkPageAvailable|. By default, all pages are available.
  class PageAvailabilityManager {
   public:
    // Marks the page as busy and delays calling the callback in
    // |OnPageAvailable| for this page. It is an error to call this method for a
    // page that is already busy.
    void MarkPageBusy(convert::ExtendedStringView page_id);

    // Marks the page as available and calls any pending callbacks from
    // |OnPageAvailable| for this page.
    void MarkPageAvailable(convert::ExtendedStringView page_id);

    // If the page is available calls the given callback directly. Otherwise,
    // the callback is registered util the page becomes available.
    void OnPageAvailable(convert::ExtendedStringView page_id,
                         fit::closure on_page_available);

    // Checks whether there are no busy pages.
    bool IsEmpty();

    void set_on_empty(fit::closure on_empty_callback);

   private:
    // Checks if the object is empty, if it is empty an an on_empty callback is
    // set, calls it.
    void CheckEmpty();

    // For each busy page, stores the list of pending callbacks.
    std::map<storage::PageId, std::vector<fit::closure>> busy_pages_;

    fit::closure on_empty_callback_;
  };

  // Requests a PageStorage object for the given |container|. If the page is not
  // locally available, the |callback| is called with |PAGE_NOT_FOUND|.
  void InitPageManagerContainer(PageManagerContainer* container,
                                convert::ExtendedStringView page_id,
                                fit::function<void(storage::Status)> callback);

  // Creates a page storage for the given |page_id| and completes the
  // PageManagerContainer.
  void CreatePageStorage(storage::PageId page_id, PageState page_state,
                         PageManagerContainer* container);

  // Adds a new PageManagerContainer for |page_id| and configures it so that it
  // is automatically deleted from |page_managers_| when the last local client
  // disconnects from the page. Returns the container.
  PageManagerContainer* AddPageManagerContainer(storage::PageIdView page_id);

  // Creates a new page manager for the given storage.
  std::unique_ptr<PageManager> NewPageManager(
      std::unique_ptr<storage::PageStorage> page_storage,
      PageManager::PageStorageState state);

  // Checks whether the given page is closed and staisfies the given
  // |predicate|. The result returned in the callback will be |PAGE_OPENED| if
  // the page is opened after calling this method and before the callback is
  // called. Otherwise it will be |YES| or |NO| depending on whether the
  // predicate is satisfied.
  void PageIsClosedAndSatisfiesPredicate(
      storage::PageIdView page_id,
      fit::function<void(PageManager*,
                         fit::function<void(storage::Status, bool)>)>
          predicate,
      fit::function<void(storage::Status, PagePredicateResult)> callback);

  // Returns a tracking Callable object for the given page. When called, returns
  // |true| if the page has not been opened until now, and stops tracking the
  // page.
  PageTracker NewPageTracker(storage::PageIdView page_id);

  // If the page is among the ones whose usage is being tracked, marks this page
  // as opened. See also |page_was_opened_map_|.
  void MaybeMarkPageOpened(storage::PageIdView page_id);

  void CheckEmpty();

  Environment* const environment_;
  std::string ledger_name_;
  std::unique_ptr<encryption::EncryptionService> encryption_service_;
  // |storage_| must outlive objects containing CommitWatchers, which includes
  // |ledger_sync_| and |page_managers_|.
  std::unique_ptr<storage::LedgerStorage> storage_;
  std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync_;
  LedgerImpl ledger_impl_;
  // |merge_manager_| must be destructed after |page_managers_| to ensure it
  // outlives any page-specific merge resolver.
  LedgerMergeManager merge_manager_;
  callback::AutoCleanableSet<
      ErrorNotifierBinding<fuchsia::ledger::LedgerErrorNotifierDelegate>>
      bindings_;

  // Mapping from each page id to the manager of that page.
  callback::AutoCleanableMap<storage::PageId, PageManagerContainer,
                             convert::StringViewComparator>
      page_managers_;
  PageUsageListener* page_usage_listener_;
  fit::closure on_empty_callback_;

  PageAvailabilityManager page_availability_manager_;

  // |page_was_opened_map_| is used to track whether certain pages were opened
  // during a given operation. When |PageIsClosedAndSatisfiesPredicate()| is
  // called, an entry is added in this map, with the given page_id as key, while
  // a unique operation id is added in the corresponding value. That entry will
  // be deleted either when that operation is done, or when the page is opened
  // because of an external request. This guarantees that if before calling the
  // callback of |PageIsClosedAndSatisfiesPredicate|, the entry is still present
  // in the map, the page was not opened during that operation. Otherwise, it
  // was, and |PAGE_OPENED| should be returned.
  std::map<storage::PageId, std::vector<uint64_t>> page_was_opened_map_;
  uint64_t page_was_opened_id_ = 0;
  // |tracked_pages_| counts the number of active page-tracking operations. The
  // manager is not empty until all operations have completed.
  uint64_t tracked_pages_ = 0;

  // Must be the last member.
  fxl::WeakPtrFactory<LedgerManager> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerManager);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_LEDGER_MANAGER_H_
