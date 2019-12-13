// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_LEDGER_MANAGER_H_
#define SRC_LEDGER_BIN_APP_LEDGER_MANAGER_H_

#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <type_traits>
#include <vector>

#include "src/ledger/bin/app/ledger_impl.h"
#include "src/ledger/bin/app/merging/ledger_merge_manager.h"
#include "src/ledger/bin/app/page_availability_manager.h"
#include "src/ledger/bin/app/page_manager.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/encryption/public/encryption_service.h"
#include "src/ledger/bin/environment/environment.h"
#include "src/ledger/bin/fidl/syncable.h"
#include "src/ledger/bin/fidl/syncable/syncable_binding.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/bin/sync_coordinator/public/ledger_sync.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/memory/weak_ptr.h"
#include "src/lib/callback/auto_cleanable.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

// Manages a ledger instance. A ledger instance represents the data scoped to a
// particular user and a particular client app.
//
// LedgerManager owns all per-ledger-instance objects: LedgerStorage and a FIDL
// LedgerImpl. It is safe to delete it at any point - this closes all channels,
// deletes the LedgerImpl and tears down the storage.
class LedgerManager : public LedgerImpl::Delegate, inspect_deprecated::ChildrenManager {
 public:
  LedgerManager(Environment* environment, std::string ledger_name,
                inspect_deprecated::Node inspect_node,
                std::unique_ptr<encryption::EncryptionService> encryption_service,
                std::unique_ptr<storage::LedgerStorage> storage,
                std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync,
                std::vector<PageUsageListener*> page_usage_listeners);
  LedgerManager(const LedgerManager&) = delete;
  LedgerManager& operator=(const LedgerManager&) = delete;
  ~LedgerManager() override;

  // Creates a new proxy for the LedgerImpl managed by this LedgerManager.
  void BindLedger(fidl::InterfaceRequest<Ledger> ledger_request);

  // Checks whether the given page is closed and synced. The result returned in
  // the callback will be |PAGE_OPENED| if the page is opened after calling this
  // method and before the callback is called. Otherwise it will be |YES| or
  // |NO| depending on whether the page is synced or not.
  void PageIsClosedAndSynced(storage::PageIdView page_id,
                             fit::function<void(Status, PagePredicateResult)> callback);

  // Checks whether the given page is closed, offline and empty. The result
  // returned in the callback will be |PAGE_OPENED| if the page is opened after
  // calling this method and before the callback is called. Otherwise it will be
  // |YES| or |NO| depending on whether the page is offline and empty or not.
  void PageIsClosedOfflineAndEmpty(storage::PageIdView page_id,
                                   fit::function<void(Status, PagePredicateResult)> callback);

  // Deletes the local copy of the page. If the page is currently open, the
  // callback will be called with |ILLEGAL_STATE|.
  void DeletePageStorage(convert::ExtendedStringView page_id, fit::function<void(Status)> callback);

  // Tries to open the closed page and start a sync with the cloud.
  void TrySyncClosedPage(convert::ExtendedStringView page_id);

  // LedgerImpl::Delegate:
  void GetPage(convert::ExtendedStringView page_id, PageState page_state,
               fidl::InterfaceRequest<Page> page_request,
               fit::function<void(Status)> callback) override;
  void SetConflictResolverFactory(fidl::InterfaceHandle<ConflictResolverFactory> factory) override;

  // inspect_deprecated::ChildrenManager:
  void GetNames(fit::function<void(std::set<std::string>)> callback) override;
  void Attach(std::string name, fit::function<void(fit::closure)> callback) override;

  void SetOnDiscardable(fit::closure on_discardable);
  bool IsDiscardable() const;

  // Registers "interest" in this LedgerManager for which this LedgerManager
  // will remain non-empty and returns a closure that when called will
  // deregister the "interest" in this LedgerManager (and potentially cause this
  // LedgerManager's on_discardable_ to be called).
  fit::closure CreateDetacher();

 private:
  using PageTracker = fit::function<bool()>;

  // Retrieves (if present in |page_managers_| when called) or creates and
  // places in |page_managers_| (if not present in |page_managers_| when called)
  // the |PageManager| for the given |page_id|.
  // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=12323): This
  // method's return value should be an interest-indication "retainer" object
  // that when deleted indicates to the got-or-created |PageManager| that it
  // should check its emptiness and possibly call its on_discardable.
  PageManager* GetOrCreatePageManager(convert::ExtendedStringView page_id);

  void CheckDiscardable();

  Environment* const environment_;
  std::string ledger_name_;

  // A nonnegative count of the number of "registered interests" for this
  // |LedgerManager|. This field is incremented by calls to |CreateDetacher| and
  // decremented by calls to the closures returned by calls to |CreateDetacher|.
  // This |LedgerManager| is not considered empty while this number is positive.
  int64_t outstanding_detachers_ = 0;

  std::unique_ptr<encryption::EncryptionService> encryption_service_;
  // |storage_| must outlive objects containing CommitWatchers, which includes
  // |ledger_sync_| and |active_page_manager_containers_|.
  std::unique_ptr<storage::LedgerStorage> storage_;
  std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync_;
  LedgerImpl ledger_impl_;
  // |merge_manager_| must be destructed after |active_page_manager_containers_|
  // to ensure it outlives any page-specific merge resolver.
  LedgerMergeManager merge_manager_;
  callback::AutoCleanableSet<SyncableBinding<fuchsia::ledger::LedgerSyncableDelegate>> bindings_;

  // Mapping from each page id to the manager of that page.
  callback::AutoCleanableMap<storage::PageId, PageManager, convert::StringViewComparator>
      page_managers_;
  std::vector<PageUsageListener*> page_usage_listeners_;
  fit::closure on_discardable_;

  // The static Inspect object maintaining in Inspect a representation of this
  // LedgerManager.
  inspect_deprecated::Node inspect_node_;
  // The static Inspect object to which this LedgerManager's pages are attached.
  inspect_deprecated::Node pages_node_;
  fit::deferred_callback children_manager_retainer_;

  // Must be the last member.
  WeakPtrFactory<LedgerManager> weak_factory_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_LEDGER_MANAGER_H_
