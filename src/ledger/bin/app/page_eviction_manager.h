// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_H_
#define SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_H_

#include <lib/fit/function.h>

#include <map>
#include <string>

#include "src/ledger/bin/app/page_eviction_policies.h"
#include "src/ledger/bin/app/page_usage_listener.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

// Manages page eviction based on page usage information.
//
// |PageEvictionManager| provides the |TryEvictPage| method which evicts a given
// page, as well as one that selects and evicts a set of pages, among those
// that are stored on the device.
//
// Regardless of the method used, a page can only be evicted if it is closed,
// i.e. is not currently opened by an external request, and either of the
// following is true:
// - All contents of the page (commits and objects) are synced to the cloud.
// - The page is offline and empty. A page is offline if it has never been
//   synced to the cloud or a peer. It is empty, if it has a single head commit
//   and the contents of that commit is empty.
//
// If neither of these conditions is fulfilled, the page will fail to be
// evicted.
class PageEvictionManager {
 public:
  // A Delegate, providing the necessary functionality to allow
  // PageEvictionManager to perform storage clean up operations.
  class Delegate {
   public:
    // Checks whether the given page is closed and synced. The result returned
    // in the callback will be |PAGE_OPENED| if the page is opened after calling
    // this method and before the callback is called. Otherwise it will be |YES|
    // or |NO| depending on whether the page is synced.
    virtual void PageIsClosedAndSynced(
        absl::string_view ledger_name, storage::PageIdView page_id,
        fit::function<void(Status, PagePredicateResult)> callback) = 0;

    // Checks whether the given page is closed, offline and empty. The result
    // returned in the callback will be |PAGE_OPENED| if the page is opened
    // after calling this method and before the callback is called. Otherwise it
    // will be |YES| or |NO| depending on whether the page is offline and empty.
    virtual void PageIsClosedOfflineAndEmpty(
        absl::string_view ledger_name, storage::PageIdView page_id,
        fit::function<void(Status, PagePredicateResult)> callback) = 0;

    // Deletes the local copy of the given page from storage.
    virtual void DeletePageStorage(absl::string_view ledger_name, storage::PageIdView page_id,
                                   fit::function<void(Status)> callback) = 0;
  };

  PageEvictionManager() = default;
  PageEvictionManager(const PageEvictionManager&) = delete;
  PageEvictionManager& operator=(const PageEvictionManager&) = delete;
  virtual ~PageEvictionManager() = default;

  // Sets the callback to be called every time the PageEvictionManager is empty.
  virtual void SetOnDiscardable(fit::closure on_discardable) = 0;

  // Returns whether the PageEvictionManager is empty, i.e. whether there are no
  // pending operations.
  virtual bool IsDiscardable() const = 0;

  // Tries to evict from the local storage the least recently used page among
  // those that are not currectly in use and can be evicted. Returns |IO_ERROR|
  // through the callback in case of failure to retrieve data on page usage, or
  // when trying to evict a given page; |OK| otherwise. It is not an error if
  // there is no page fulfilling the requirements.
  virtual void TryEvictPages(PageEvictionPolicy* policy, fit::function<void(Status)> callback) = 0;

  // Marks the page as open.
  virtual void MarkPageOpened(absl::string_view ledger_name, storage::PageIdView page_id) = 0;

  // Marks the page as closed.
  virtual void MarkPageClosed(absl::string_view ledger_name, storage::PageIdView page_id) = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_EVICTION_MANAGER_H_
