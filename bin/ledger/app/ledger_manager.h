// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_LEDGER_MANAGER_H_
#define APPS_LEDGER_SRC_APP_LEDGER_MANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <type_traits>

#include "apps/ledger/src/app/ledger_impl.h"
#include "apps/ledger/src/app/merging/ledger_merge_manager.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/environment/environment.h"
#include "apps/ledger/src/storage/public/types.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace ledger {

// Manages a ledger instance. A ledger instance represents the data scoped to a
// particular user and a particular client app.
//
// LedgerManager owns all per-ledger-instance objects: LedgerStorage and a FIDL
// LedgerImpl. It is safe to delete it at any point - this closes all channels,
// deletes the LedgerImpl and tears down the storage.
class LedgerManager : public LedgerImpl::Delegate {
 public:
  LedgerManager(Environment* environment,
                std::unique_ptr<storage::LedgerStorage> storage,
                std::unique_ptr<cloud_sync::LedgerSync> sync);
  ~LedgerManager() override;

  // Creates a new proxy for the LedgerImpl managed by this LedgerManager.
  void BindLedger(fidl::InterfaceRequest<Ledger> ledger_request);

  // LedgerImpl::Delegate:
  void GetPage(convert::ExtendedStringView page_id,
               fidl::InterfaceRequest<Page> page_request,
               std::function<void(Status)> callback) override;
  Status DeletePage(convert::ExtendedStringView page_id) override;
  void SetConflictResolverFactory(
      fidl::InterfaceHandle<ConflictResolverFactory> factory) override;

  void set_on_empty(const fxl::Closure& on_empty_callback) {
    on_empty_callback_ = on_empty_callback;
  }

 private:
  class PageManagerContainer;

  // Creates a page storage for the given |page_id| and completes the
  // PageManagerContainer.
  void CreatePageStorage(storage::PageId page_id,
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

  Environment* const environment_;
  std::unique_ptr<storage::LedgerStorage> storage_;
  std::unique_ptr<cloud_sync::LedgerSync> sync_;
  LedgerImpl ledger_impl_;
  // |merge_manager_| must be destructed after |page_managers_| to ensure it
  // outlives any page-specific merge resolver.
  LedgerMergeManager merge_manager_;
  fidl::BindingSet<Ledger> bindings_;

  // Mapping from each page id to the manager of that page.
  callback::AutoCleanableMap<storage::PageId,
                             PageManagerContainer,
                             convert::StringViewComparator>
      page_managers_;
  fxl::Closure on_empty_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerManager);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_LEDGER_MANAGER_H_
