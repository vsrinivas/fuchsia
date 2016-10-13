// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_APP_LEDGER_MANAGER_H_
#define APPS_LEDGER_APP_LEDGER_MANAGER_H_

#include <functional>
#include <map>
#include <memory>
#include <type_traits>

#include "apps/ledger/app/ledger_impl.h"
#include "apps/ledger/convert/convert.h"
#include "apps/ledger/storage/public/types.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/strings/string_view.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace ledger {

// Manages a ledger instance. Ledger instance represents the data scoped to a
// particular user and a particular client app.
//
// LedgerManager owns all per-ledger-instance objects: LedgerStorage and a Mojo
// LedgerImpl. It is safe to delete it at any point - this closes all message
// pipes, deletes the LedgerImpl and tears down the storage.
class LedgerManager : public LedgerImpl::Delegate {
 public:
  LedgerManager(std::unique_ptr<storage::LedgerStorage> storage);
  ~LedgerManager();

  // Creates a new proxy for the LedgerImpl managed by this LedgerManager.
  LedgerPtr GetLedgerPtr();

  // LedgerImpl::Delegate:
  void CreatePage(std::function<void(Status, PagePtr)> callback) override;
  void GetPage(ftl::StringView page_id,
               CreateIfNotFound create_if_not_found,
               std::function<void(Status, PagePtr)> callback) override;
  Status DeletePage(ftl::StringView page_id) override;

 private:
  // Adds a new PageManager for |page_id| and configures its so that we delete
  // it from |page_managers_| automatically when the last local client
  // disconnects from the page. Returns a new PagePtr bound to this manager.
  PagePtr AddPageManagerAndGetPagePtr(
      storage::PageIdView page_id,
      std::unique_ptr<storage::PageStorage> page_storage);

  std::unique_ptr<storage::LedgerStorage> storage_;
  LedgerImpl ledger_impl_;
  mojo::BindingSet<Ledger> bindings_;

  // Mapping from page id to the manager of that page.
  std::map<storage::PageId,
           std::unique_ptr<PageManager>,
           convert::StringViewComparator>
      page_managers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LedgerManager);
};

}  // namespace ledger

#endif  // APPS_LEDGER_APP_LEDGER_MANAGER_H_
