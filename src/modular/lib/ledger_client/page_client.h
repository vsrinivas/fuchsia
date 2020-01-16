// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_LEDGER_CLIENT_PAGE_CLIENT_H_
#define SRC_MODULAR_LIB_LEDGER_CLIENT_PAGE_CLIENT_H_

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>

#include <array>
#include <string>

#include "src/lib/fxl/macros.h"
#include "src/modular/lib/ledger_client/types.h"

namespace modular {

class LedgerClient;

// A helper class that aids in interfacing with a fuchsia.ledger.Page by:
//
// 1) Forwarding requests for conflict resolution from the
// fuchsia.ledger.Ledger connection to a PageClient's OnPageConflict() which is
// constructed with an associated key prefix of the Page.
// 2) Providing a convenient method to acquire a PageSnapshot from the Page.
// 3) Providing an optional and convenient per-key
// fuchsia.ledger.PageWatcher.OnChange() implementation that calls into
// OnPageChange(). Clients that care about the notification semantics of >1 key
// at a time may wish to implement OnChange() directly.
//
// NOTE: The conflict resolution API is currently implemented on a per-key
// basis.  Conflict resolution may be difficult for some clients to implement
// if a multiple-key update has semantic meaning. MF-157
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
  PageClient(std::string context, LedgerClient* ledger_client, LedgerPageId page_id,
             std::string prefix = "");
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
  fuchsia::ledger::PageSnapshotPtr NewSnapshot();

  const fuchsia::ledger::PageId& page_id() const { return page_id_; }
  const std::string& prefix() const { return prefix_; }
  fuchsia::ledger::Page* page() { return page_; }

  // Computed by implementations of OnPageConflict() in derived classes.
  enum ConflictResolution { LEFT, RIGHT, MERGE };

  // The argument to OnPageConflict(). It's mutated in place so it's easier to
  // extend without having to alter clients.
  struct Conflict {
    std::vector<uint8_t> key;

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

 protected:
  // Derived classes may implement this method as needed. The default
  // implementation copies the VMO to a string and forwards to
  // |OnPageChange(const std::string&, const std::string&)|.
  virtual void OnPageChange(const std::string& key, fuchsia::mem::BufferPtr value);

  using PageWatcher::OnChangeCallback;
  // |PageWatcher|
  //
  // Derived classes may implement this method as needed. The default
  // implementation forwards individual changed keys to OnPageChange() and
  // OnPageDelete().
  void OnChange(fuchsia::ledger::PageChange page, fuchsia::ledger::ResultState result_state,
                OnChangeCallback callback) override;

 private:
  // Derived classes implement this method as needed. The default implementation
  // does nothing. This method is only called if forwarded from
  // |OnPageChange(const std::string&, fuchsia::mem::BufferPtr)|.
  virtual void OnPageChange(const std::string& key, const std::string& value);
  // Derived classes implement this method as needed. The default implementation
  // does nothing.
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
                std::vector<fuchsia::ledger::Entry>* entries, fit::function<void()> done);

}  // namespace modular

#endif  // SRC_MODULAR_LIB_LEDGER_CLIENT_PAGE_CLIENT_H_
