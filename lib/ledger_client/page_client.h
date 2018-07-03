// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_LEDGER_CLIENT_PAGE_CLIENT_H_
#define PERIDOT_LIB_LEDGER_CLIENT_PAGE_CLIENT_H_

#include <array>
#include <string>

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/ledger_client/types.h"

namespace modular {

class LedgerClient;

// A helper class that holds on to a page snapshot through a shared
// pointer, hands out shared pointers to it, can replace the snapshot
// by a new one, and registers a connection error handler on it. It
// essentially wraps fuchsia::ledger::PageSnapshot.
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
class PageClient : fuchsia::ledger::PageWatcher {
 public:
  // |context| is used as a string prefix on log messages.  |ledger_client| is
  // used to retrieve a handle to the page specified in |page_id|, and to
  // listen for conflicts from the ledger. If |prefix| is provided, the
  // resulting page snapshot and change notifications are limited to only keys
  // with that prefix. However, OnPageChange()'s |key| will include the full
  // key, including the prefix.
  //
  // |ledger_client| must outlive *this.
  PageClient(std::string context, LedgerClient* ledger_client,
             LedgerPageId page_id, std::string prefix = "");
  ~PageClient() override;

  // Returns a snapshot of the Ledger page under |prefix| at the most recent
  // timepoint.
  //
  // If |on_error| is provided, it will be called if there was a Ledger error
  // trying to get the snapshot.
  //
  // NOTE: There is no guaranteed timing for writes made to the returned
  // PageSnapshot and notifications of changes through OnPageChange(). The
  // ordering is guaranteed to be the same, ignoring changes to the writes
  // caused by conflict resolution which can cause some writes to disappear.
  fuchsia::ledger::PageSnapshotPtr NewSnapshot(
      std::function<void()> on_error = nullptr);

  const fuchsia::ledger::PageId& page_id() const { return page_id_; }
  const std::string& prefix() const { return prefix_; }
  fuchsia::ledger::Page* page() { return page_; }

  // Computed by implementations of OnPageConflict() in derived classes.
  enum ConflictResolution { LEFT, RIGHT, MERGE };

  // The argument to OnPageConflict(). It's mutated in place so it's easier to
  // extend without having to alter clients.
  struct Conflict {
    fidl::VectorPtr<uint8_t> key;

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

  // |PageWatcher|
  void OnChange(fuchsia::ledger::PageChange page,
                fuchsia::ledger::ResultState result_state,
                OnChangeCallback callback) override;

  fidl::Binding<fuchsia::ledger::PageWatcher> binding_;
  const std::string context_;

  LedgerClient* const ledger_client_;
  const fuchsia::ledger::PageId page_id_;
  fuchsia::ledger::Page* const page_;
  const std::string prefix_;

  FXL_DISALLOW_COPY_AND_ASSIGN(PageClient);
};

// Retrieves all entries from the given snapshot and calls the given callback
// with the final status.
//
// The FIDL pointer backing |snapshot| must have the same life time as
// |entries|, so that callbacks are cancelled when |entries| are deleted before
// |callback| is invoked.
void GetEntries(fuchsia::ledger::PageSnapshot* snapshot,
                std::vector<fuchsia::ledger::Entry>* entries,
                std::function<void(fuchsia::ledger::Status)> callback);

}  // namespace modular

#endif  // PERIDOT_LIB_LEDGER_CLIENT_PAGE_CLIENT_H_
