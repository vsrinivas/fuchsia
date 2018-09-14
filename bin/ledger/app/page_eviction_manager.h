// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_H_

#include <map>
#include <string>

#include <lib/fit/function.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/app/page_usage_listener.h"
#include "peridot/bin/ledger/app/types.h"
#include "peridot/bin/ledger/fidl/include/types.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

// Manages page eviction based on page usage information.
class PageEvictionManager : public PageUsageListener {
 public:
  // A Delegate, providing the necessary functionality to allow
  // PageEvictionManager to perform storage clean up operations.
  class Delegate {
   public:
    // Checks whether the given page is closed and synced. The result returned
    // in the callback will be |PageClosedAndSynced:UNKNOWN| if the page is
    // opened after calling this method and before the callback is called.
    // Otherwise it will be |YES| or |NO| depending on whether the page is
    // synced.
    virtual void PageIsClosedAndSynced(
        fxl::StringView ledger_name, storage::PageIdView page_id,
        fit::function<void(Status, PageClosedAndSynced)> callback) = 0;

    // Checks whether the given page is closed, offline and empty. The result
    // returned in the callback will be |PageClosedOfflineAndEmpty:UNKNOWN| if
    // the page is opened after calling this method and before the callback is
    // called. Otherwise it will be |YES| or |NO| depending on whether the page
    // is offline and empty.
    virtual void PageIsClosedOfflineAndEmpty(
        fxl::StringView ledger_name, storage::PageIdView page_id,
        fit::function<void(Status, PageClosedOfflineAndEmpty)> callback) = 0;

    // Deletes the local copy of the given page from storage.
    virtual void DeletePageStorage(fxl::StringView ledger_name,
                                   storage::PageIdView page_id,
                                   fit::function<void(Status)> callback) = 0;
  };

  PageEvictionManager() {}
  ~PageEvictionManager() override {}

  // Sets the callback to be called every time the PageEvictionManager is empty.
  virtual void set_on_empty(fit::closure on_empty_callback) = 0;

  // Returns whether the PageEvictionManager is empty, i.e. whether there are no
  // pending operations.
  virtual bool IsEmpty() = 0;

  // Tries to evict from the local storage the least recently used page among
  // those that have been backed up in the cloud and are not currectly in use.
  // Returns |IO_ERROR| through the callback in case of failure to retrieve data
  // on page usage, or when trying to evict a given page; |OK| otherwise. It is
  // not an error if there is no page fulfilling the requirements.
  virtual void TryEvictPages(fit::function<void(Status)> callback) = 0;

  virtual void EvictIfEmpty(fxl::StringView ledger_name,
                            storage::PageIdView page_id,
                            fit::function<void(Status)> callback) = 0;

  // PageUsageListener:
  void OnPageOpened(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override = 0;
  void OnPageClosed(fxl::StringView ledger_name,
                    storage::PageIdView page_id) override = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageEvictionManager);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_EVICTION_MANAGER_H_
