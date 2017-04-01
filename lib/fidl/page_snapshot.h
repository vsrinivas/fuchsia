// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_FIDL_PAGE_SNAPSHOT_H_
#define APPS_MODULAR_LIB_FIDL_PAGE_SNAPSHOT_H_

#include <string>

#include "apps/ledger/services/public/ledger.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace modular {

// A helper class that holds on to a page snapshot through a shared
// pointer, hands out shared pointers to it, can replace the snapshot
// by a new one, and registers a connection error handler on it. It
// essentially wraps ledger::PageSnapshot, hence the name.
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
// a duplicate PageSnaphotPtr for each Operation instance that needs
// one, but PageSnapshot doesn't have a duplicate method.
class PageSnapshot {
 public:
  // Takes a context name as a label for the error messages it logs.
  explicit PageSnapshot(const std::string& context);
  ~PageSnapshot();

  // Replaces the previous page snapshot with a newly requested one.
  fidl::InterfaceRequest<ledger::PageSnapshot> NewRequest();

  // Returns the current page snapshot.
  std::shared_ptr<ledger::PageSnapshotPtr> shared_ptr() {
    return page_snapshot_;
  }

 private:
  const std::string context_;
  std::shared_ptr<ledger::PageSnapshotPtr> page_snapshot_;
  FTL_DISALLOW_COPY_AND_ASSIGN(PageSnapshot);
};

}  // namespace modular

#endif  // APPS_MODULAR_LIB_FIDL_PAGE_SNAPSHOT_H_
