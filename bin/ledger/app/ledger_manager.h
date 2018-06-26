// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_LEDGER_MANAGER_H_
#define PERIDOT_BIN_LEDGER_APP_LEDGER_MANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <type_traits>

#include <fuchsia/ledger/internal/cpp/fidl.h>

#include "lib/callback/auto_cleanable.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/app/ledger_impl.h"
#include "peridot/bin/ledger/app/merging/ledger_merge_manager.h"
#include "peridot/bin/ledger/app/page_eviction_manager.h"
#include "peridot/bin/ledger/app/page_manager.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/bin/ledger/environment/environment.h"
#include "peridot/bin/ledger/storage/public/types.h"
#include "peridot/bin/ledger/sync_coordinator/public/ledger_sync.h"
#include "peridot/lib/convert/convert.h"

namespace ledger {

// Manages a ledger instance. A ledger instance represents the data scoped to a
// particular user and a particular client app.
//
// LedgerManager owns all per-ledger-instance objects: LedgerStorage and a FIDL
// LedgerImpl. It is safe to delete it at any point - this closes all channels,
// deletes the LedgerImpl and tears down the storage.
class LedgerManager : public LedgerImpl::Delegate,
                      public ledger_internal::LedgerDebug {
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

  // LedgerImpl::Delegate:
  void GetPage(convert::ExtendedStringView page_id, PageState page_state,
               fidl::InterfaceRequest<Page> page_request,
               std::function<void(Status)> callback) override;
  Status DeletePage(convert::ExtendedStringView page_id) override;
  void SetConflictResolverFactory(
      fidl::InterfaceHandle<ConflictResolverFactory> factory) override;

  void set_on_empty(const fxl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

  // Creates a new proxy for the LedgerDebug implemented by this LedgerManager.
  void BindLedgerDebug(fidl::InterfaceRequest<LedgerDebug> request);

 private:
  class PageManagerContainer;

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

  void CheckEmpty();

  // LedgerDebug:
  void GetPagesList(GetPagesListCallback callback) override;

  void GetPageDebug(
      ledger::PageId page_id,
      fidl::InterfaceRequest<ledger_internal::PageDebug> page_debug,
      GetPageDebugCallback callback) override;

  Environment* const environment_;
  std::string ledger_name_;
  std::unique_ptr<encryption::EncryptionService> encryption_service_;
  std::unique_ptr<storage::LedgerStorage> storage_;
  std::unique_ptr<sync_coordinator::LedgerSync> ledger_sync_;
  LedgerImpl ledger_impl_;
  // |merge_manager_| must be destructed after |page_managers_| to ensure it
  // outlives any page-specific merge resolver.
  LedgerMergeManager merge_manager_;
  fidl::BindingSet<Ledger> bindings_;

  // Mapping from each page id to the manager of that page.
  callback::AutoCleanableMap<storage::PageId, PageManagerContainer,
                             convert::StringViewComparator>
      page_managers_;
  PageUsageListener* page_usage_listener_;
  fxl::Closure on_empty_callback_;

  fidl::BindingSet<LedgerDebug> ledger_debug_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerManager);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_LEDGER_MANAGER_H_
