// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_LEDGER_CLIENT_LEDGER_CLIENT_H_
#define PERIDOT_LIB_LEDGER_CLIENT_LEDGER_CLIENT_H_

#include <functional>
#include <memory>
#include <vector>

#include <fuchsia/ledger/cpp/fidl.h>
#include <fuchsia/ledger/internal/cpp/fidl.h>
#include <lib/async/cpp/operation.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/ledger_client/types.h"

namespace modular {

class PageClient;

// The primary purpose of the ledger client is to act as conflict resolver
// factory which is able to dispatch conflicts to the page clients based on
// their page and key prefix.
class LedgerClient : fuchsia::ledger::ConflictResolverFactory {
 public:
  LedgerClient(fuchsia::ledger::internal::LedgerRepository* ledger_repository,
               const std::string& name, std::function<void()> error);
  LedgerClient(fuchsia::ledger::LedgerPtr ledger);
  ~LedgerClient() override;

  fuchsia::ledger::Ledger* ledger() const { return ledger_.get(); }

  // A callback that is invoked every time one conflict resolution completes.
  // Used only for testing so far.
  void add_watcher(std::function<void()> watcher) {
    watchers_.emplace_back(std::move(watcher));
  }

  // Creates a new LedgerClient instance connected to the same Ledger as the
  // LedgerClient it was obtained from, but it creates new Page connections for
  // its PageClients. This allows the creation of PageClients that behave as if
  // they run on another device. Useful to simplify testing of cross device
  // behavior. See fuchsia::modular::StoryProvider.GetLinkPeer().
  //
  // The Peer instance does NOT register itself as a conflict resolver for the
  // Ledger, as the primary LedgerClient must remain the conflict resolver.
  // (Ledger currently does not support registration of multiple conflict
  // resolvers for the same ledger, even on different connections. Later
  // registrations simply overwrite earlier ones.)
  std::unique_ptr<LedgerClient> GetLedgerClientPeer();

 private:
  // Supports GetLedgerClientPeer().
  LedgerClient(fuchsia::ledger::internal::LedgerRepository* ledger_repository,
               const std::string& name);

  friend class PageClient;
  class ConflictResolverImpl;
  struct PageEntry;

  // Used by PageClient to access a new page on creation. Two page clients of
  // the same page share the same ledger::Page connection.
  fuchsia::ledger::Page* GetPage(PageClient* page_client,
                                 const std::string& context,
                                 const fuchsia::ledger::PageId& page_id);

  // PageClient deregisters itself on destrution.
  void DropPageClient(PageClient* page_client);

  // |ConflictResolverFactory|
  void GetPolicy(LedgerPageId page_id, GetPolicyCallback callback) override;

  // |ConflictResolverFactory|
  void NewConflictResolver(
      LedgerPageId page_id,
      fidl::InterfaceRequest<fuchsia::ledger::ConflictResolver> request)
      override;

  void ClearConflictResolver(const LedgerPageId& page_id);

  // Supports GetLedgerClientPeer().
  fuchsia::ledger::internal::LedgerRepositoryPtr ledger_repository_;
  const std::string ledger_name_;

  // The ledger this is a client of.
  fuchsia::ledger::LedgerPtr ledger_;

  fidl::BindingSet<fuchsia::ledger::ConflictResolverFactory> bindings_;
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
class LedgerClient::ConflictResolverImpl : fuchsia::ledger::ConflictResolver {
 public:
  ConflictResolverImpl(LedgerClient* ledger_client,
                       const LedgerPageId& page_id);
  ~ConflictResolverImpl() override;

  void Connect(
      fidl::InterfaceRequest<fuchsia::ledger::ConflictResolver> request);

  const LedgerPageId& page_id() const { return page_id_; }

 private:
  // |ConflictResolver|
  void Resolve(
      fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> left_version,
      fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> right_version,
      fidl::InterfaceHandle<fuchsia::ledger::PageSnapshot> common_version,
      fidl::InterfaceHandle<fuchsia::ledger::MergeResultProvider>
          result_provider) override;

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

#endif  // PERIDOT_LIB_LEDGER_CLIENT_LEDGER_CLIENT_H_
