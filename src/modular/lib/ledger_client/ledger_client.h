// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_LEDGER_CLIENT_LEDGER_CLIENT_H_
#define SRC_MODULAR_LIB_LEDGER_CLIENT_LEDGER_CLIENT_H_

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr.h>

#include <functional>
#include <memory>
#include <vector>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/modular/lib/async/cpp/operation.h"
#include "src/modular/lib/ledger_client/types.h"

namespace modular {

class PageClient;

// The primary purpose of the ledger client is to act as conflict resolver
// factory which is able to dispatch conflicts to the page clients based on
// their page and key prefix.
class LedgerClient : fuchsia::ledger::ConflictResolverFactory {
 public:
  LedgerClient(fuchsia::ledger::internal::LedgerRepository* ledger_repository,
               const std::string& name, fit::function<void(zx_status_t status)> error);
  LedgerClient(fuchsia::ledger::LedgerPtr ledger, fit::function<void(zx_status_t status)> error);
  ~LedgerClient() override;

  fuchsia::ledger::Ledger* ledger() const { return ledger_.get(); }

  // A callback that is invoked every time one conflict resolution completes.
  // Used only for testing so far.
  void add_watcher(fit::function<void()> watcher) { watchers_.emplace_back(std::move(watcher)); }

 private:
  friend class PageClient;
  class ConflictResolverImpl;
  struct PageEntry;

  // Used by PageClient to access a new page on creation. Two page clients of
  // the same page share the same ledger::Page connection.
  fuchsia::ledger::Page* GetPage(PageClient* page_client, const std::string& context,
                                 const fuchsia::ledger::PageId& page_id);

  // PageClient deregisters itself on destrution.
  void DropPageClient(PageClient* page_client);

  // |ConflictResolverFactory|
  void GetPolicy(LedgerPageId page_id, GetPolicyCallback callback) override;

  // |ConflictResolverFactory|
  void NewConflictResolver(
      LedgerPageId page_id,
      fidl::InterfaceRequest<fuchsia::ledger::ConflictResolver> request) override;

  void ClearConflictResolver(const LedgerPageId& page_id);

  // The ledger this is a client of.
  fuchsia::ledger::LedgerPtr ledger_;

  fidl::BindingSet<fuchsia::ledger::ConflictResolverFactory> bindings_;
  std::vector<std::unique_ptr<ConflictResolverImpl>> resolvers_;

  // ledger::Page connections are owned by LedgerClient, and only handed to
  // PageClient as naked pointers. This allows multiple clients of the same page
  // to share a page connection.
  std::vector<std::unique_ptr<PageEntry>> pages_;

  // Notified whenever a conflict resolution cycle finishes.
  std::vector<fit::function<void()>> watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerClient);
};

// A conflict resolver for one page that delegates the diff for a key to the
// appropriate page client that handles that key.
class LedgerClient::ConflictResolverImpl : fuchsia::ledger::ConflictResolver {
 public:
  ConflictResolverImpl(LedgerClient* ledger_client, const LedgerPageId& page_id);
  ~ConflictResolverImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::ledger::ConflictResolver> request);

  const LedgerPageId& page_id() const { return page_id_; }

 private:
  // |ConflictResolver|
  void Resolve(
      fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> left_version,
      fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> right_version,
      fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> common_version,
      fidl::InterfaceHandle<fuchsia::ledger::MergeResultProvider> result_provider) override;

  void GetPageClients(std::vector<PageClient*>* page_clients);

  void NotifyWatchers() const;

  LedgerClient* const ledger_client_;
  LedgerPageId page_id_;

  fidl::BindingSet<fuchsia::ledger::ConflictResolver> bindings_;

  OperationQueue operation_queue_;
  class ResolveCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConflictResolverImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_LIB_LEDGER_CLIENT_LEDGER_CLIENT_H_
