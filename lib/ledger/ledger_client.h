// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_LEDGER_LEDGER_CLIENT_H_
#define APPS_MODULAR_LIB_LEDGER_LEDGER_CLIENT_H_

#include <functional>
#include <memory>
#include <vector>

#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/bin/ledger/fidl/internal.fidl.h"
#include "peridot/lib/fidl/operation.h"
#include "peridot/lib/ledger/types.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace modular {

class PageClient;

// The primary purpose of the ledger client is to act as conflict resolver
// factory which is able to dispatche conflicts to the page clients based on
// their page and key prefix.
class LedgerClient : ledger::ConflictResolverFactory {
 public:
  LedgerClient(ledger::LedgerRepository* ledger_repository, const std::string& name,
               std::function<void()> error);
  ~LedgerClient() override;

  ledger::Ledger* ledger() const { return ledger_.get(); }

  // A callback that is invoked every time one conflict resolution completes.
  // Used only for testing so far.
  void add_watcher(std::function<void()> watcher) {
    watchers_.emplace_back(std::move(watcher));
  }

 private:
  friend class PageClient;
  class ConflictResolverImpl;
  struct PageEntry;

  // Used by PageClient to access a new page on creation. Two page clients of
  // the same page share the same ledger::Page connection.
  ledger::Page* GetPage(PageClient* page_client,
                        const std::string& context,
                        const LedgerPageId& page_id);

  // PageClient deregisters itself on destrution.
  void DropPageClient(PageClient* page_client);

  // |ConflictResolverFactory|
  void GetPolicy(LedgerPageId page_id,
                 const GetPolicyCallback& callback) override;

  // |ConflictResolverFactory|
  void NewConflictResolver(
      LedgerPageId page_id,
      fidl::InterfaceRequest<ledger::ConflictResolver> request) override;

  void ClearConflictResolver(const LedgerPageId& page_id);

  ledger::LedgerPtr ledger_;

  fidl::BindingSet<ledger::ConflictResolverFactory> bindings_;
  std::vector<std::unique_ptr<ConflictResolverImpl>> resolvers_;

  // ledger::Page connections are owned by LedgerClient, and only handed to
  // PageClient as naked pointers. This allows multiple clients of the same page
  // to share a page connection.
  std::vector<std::unique_ptr<PageEntry>> pages_;

  // Notified whenever a conflict resolution cycle finishes.
  std::vector<std::function<void()>> watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LedgerClient);
};

// A conflict resolver for one page that delegates the diff for a key to the
// appropriate page client that handles that key.
class LedgerClient::ConflictResolverImpl : ledger::ConflictResolver {
 public:
  ConflictResolverImpl(LedgerClient* ledger_client, LedgerPageId page_id);
  ~ConflictResolverImpl() override;

  void Connect(fidl::InterfaceRequest<ledger::ConflictResolver> request);

  const LedgerPageId& page_id() const { return page_id_; }

 private:
  // |ConflictResolver|
  void Resolve(fidl::InterfaceHandle<ledger::PageSnapshot> left_version,
               fidl::InterfaceHandle<ledger::PageSnapshot> right_version,
               fidl::InterfaceHandle<ledger::PageSnapshot> common_version,
               fidl::InterfaceHandle<ledger::MergeResultProvider>
                   result_provider) override;

  void GetPageClients(std::vector<PageClient*>* page_clients);

  void NotifyWatchers() const;

  LedgerClient* const ledger_client_;
  const LedgerPageId page_id_;

  fidl::BindingSet<ledger::ConflictResolver> bindings_;

  OperationQueue operation_queue_;
  class ResolveCall;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConflictResolverImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_LEDGER_LEDGER_CLIENT_H_
