// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_LEDGER_PAGE_CLIENT_H_
#define APPS_MODULAR_LIB_LEDGER_PAGE_CLIENT_H_

#include <string>

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/ledger/fidl/ledger.fidl.h"
#include "peridot/lib/ledger/types.h"

namespace modular {

class LedgerClient;

// A helper class that holds on to a page snapshot through a shared
// pointer, hands out shared pointers to it, can replace the snapshot
// by a new one, and registers a connection error handler on it. It
// essentially wraps ledger::PageSnapshot.
//
// This is used by classes that hold onto a PageSnapshot and update it
// in return calls from PageWatcher notifications, and use Operation
// containers to access the snapshot.
//
// Every time we receive an OnChange notification, we update the page
// snapshot so we see the current state. Just in case, we also install
// a connection error handler on the snapshot connection, so we can
// log when the connection unexpectedly closes, although we cannot do
// anything else about it.
//
// An instance of this class always holds the current snapshot of the
// page we read from, as obtained from the watcher on the page. It is
// held by a shared pointer, because the update may occur while
// Operation instances that read from it are still in progress, and
// they need to hold on to the same snapshot they started with, lest
// the methods called on that snapshot never return.
//
// The same behavior as with a shared_ptr could be accomplished with
// a duplicate PageSnapshotPtr for each Operation instance that needs
// one, but PageSnapshot doesn't have a duplicate method.
class PageClient : ledger::PageWatcher {
 public:
  // Takes a context name as a label for the error messages it logs. The ledger
  // client reference is to receive conflicts from the ledger.
  explicit PageClient(std::string context,
                      LedgerClient* ledger_client,
                      LedgerPageId page_id,
                      const char* prefix);
  ~PageClient() override;

  // Returns the current page snapshot. It is returned as a shared_ptr, so that
  // it can be used in an asynchronous operation. In that case, the page
  // snapshot might be replaced by a new one from an incoming page watcher
  // notification, but the client needs to hold onto the previous one until its
  // operation completes.
  //
  // CAVEAT. To use this snapshot does not make sense for most clients (in fact
  // it's no longer used in the modular code base). If the client implements
  // page write operations and page read operations, an invariant normally
  // maintained is that a read operation returns a value from *after* a
  // preceding write (which might be a different value than the one written when
  // there were merges from network sync), but never from *before* the preceding
  // write. This invariant is maintained when the read operation uses a fresh
  // snapshot, but not when the read operation uses the latest watcher snapshot
  // (because the watcher notification from the write might not yet have arrived
  // when the read is executed). Since all modular page clients have read and
  // write operations where this invariant is desired, they all use fresh page
  // snapshots.
  std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot() {
    return page_snapshot_;
  }

  const LedgerPageId& page_id() const { return page_id_; }
  const std::string& prefix() const { return prefix_; }
  ledger::Page* page() { return page_; }

  // Computed by implementations of OnPageConflict() in derived classes.
  enum ConflictResolution { LEFT, RIGHT, MERGE };

  // The argument to OnPageConflict(). It's mutated in place so it's easier to
  // extend without having to alter clients.
  struct Conflict {
    std::string key;

    bool has_left{};
    std::string left;
    bool left_is_deleted{};

    bool has_right{};
    std::string right;
    bool right_is_deleted{};

    ConflictResolution resolution{LEFT};
    std::string merged;
    bool merged_is_deleted{};
  };

 private:
  // Derived classes implement these methods as needed. The default
  // implementation does nothing.
  virtual void OnPageChange(const std::string& key, const std::string& value);
  virtual void OnPageDelete(const std::string& key);

  // Derived classes implement this method as needed. The default implementation
  // selects left and logs an INFO about the unresolved conflict.
  //
  // For now, only per-key conflict resolution is supported by page client. If
  // we need more coherency for conflict resolution, this can be changed.
  //
  // For now, conflict resolution is synchronous. Can be changed too, for
  // example to go on an OperationQueue to wait for ongoing changes to reconcile
  // with.
  //
  // If ConflictResolution is MERGE, the result is returned in merged*. It is
  // possible that the merge of two undeleted values is to the delete the key.
  //
  // This is invoked from the conflict resolver in LedgerClient.
  friend class LedgerClient;
  virtual void OnPageConflict(Conflict* conflict);

  // Replaces the previous page snapshot with a newly requested one.
  fidl::InterfaceRequest<ledger::PageSnapshot> NewRequest();

  // Possibly replaces the previous page snapshot with a new one
  // requested through the result callback of a PageWatcher, depending
  // on the continuation code of the watcher notification.
  fidl::InterfaceRequest<ledger::PageSnapshot> Update(
      ledger::ResultState result_state);

  // |PageWatcher|
  void OnChange(ledger::PageChangePtr page,
                ledger::ResultState result_state,
                const OnChangeCallback& callback) override;

  fidl::Binding<ledger::PageWatcher> binding_;
  const std::string context_;

  LedgerClient* const ledger_client_;
  const LedgerPageId page_id_;
  ledger::Page* const page_;
  const std::string prefix_;

  std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageClient);
};

// Retrieves all entries from the given snapshot and calls the given callback
// with the final status.
void GetEntries(ledger::PageSnapshot* snapshot,
                std::vector<ledger::EntryPtr>* entries,
                std::function<void(ledger::Status)> callback);

}  // namespace modular

#endif  // APPS_MODULAR_LIB_LEDGER_PAGE_CLIENT_H_
