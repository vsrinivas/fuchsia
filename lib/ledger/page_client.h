// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_LEDGER_PAGE_CLIENT_H_
#define APPS_MODULAR_LIB_LEDGER_PAGE_CLIENT_H_

#include <string>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

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
// The same behavior was with a shared_ptr could be accomplished with
// a duplicate PageSnapshotPtr for each Operation instance that needs
// one, but PageSnapshot doesn't have a duplicate method.
class PageClient : ledger::PageWatcher {
 public:
  // Takes a context name as a label for the error messages it logs.
  explicit PageClient(const std::string& context,
                      ledger::Page* page,
                      const char* prefix);
  ~PageClient();

  // Returns the current page snapshot.
  std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot() {
    return page_snapshot_;
  }

 private:
  // Derived classes implement these methods as needed. The default
  // implementation does nothing.
  virtual void OnChange(const std::string& key, const std::string& value);
  virtual void OnDelete(const std::string& key);

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
  std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot_;

  FTL_DISALLOW_COPY_AND_ASSIGN(PageClient);
};

// Retrieves all entries from the given snapshot and calls the given callback
// with the final status.
void GetEntries(ledger::PageSnapshot* snapshot,
                std::vector<ledger::EntryPtr>* entries,
                std::function<void(ledger::Status)> callback);

}  // namespace modular

#endif  // APPS_MODULAR_LIB_LEDGER_PAGE_CLIENT_H_
