// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_AVAILABILITY_MANAGER_H_
#define SRC_LEDGER_BIN_APP_PAGE_AVAILABILITY_MANAGER_H_

#include <map>
#include <vector>

#include <lib/fit/function.h>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

// Stores whether a given page is busy or available. After |MarkPageBusy| has
// been called, all calls to |OnPageAvailable| will be delayed until a call to
// |MarkPageAvailable|. By default, all pages are available.
class PageAvailabilityManager {
 public:
  // Marks the page as busy and delays calling the callback in
  // |OnPageAvailable| for this page. It is an error to call this method for a
  // page that is already busy.
  void MarkPageBusy(convert::ExtendedStringView page_id);

  // Marks the page as available and calls any pending callbacks from
  // |OnPageAvailable| for this page.
  void MarkPageAvailable(convert::ExtendedStringView page_id);

  // If the page is available calls the given callback directly. Otherwise,
  // the callback is registered util the page becomes available.
  void OnPageAvailable(convert::ExtendedStringView page_id,
                       fit::closure on_page_available);

  // Checks whether there are no busy pages.
  bool IsEmpty();

  void set_on_empty(fit::closure on_empty_callback);

 private:
  // Checks if the object is empty; if it is empty and on_empty callback_ is
  // set, calls on_empty_callback_.
  void CheckEmpty();

  // For each busy page, stores the list of pending callbacks.
  std::map<storage::PageId, std::vector<fit::closure>> busy_pages_;

  fit::closure on_empty_callback_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_AVAILABILITY_MANAGER_H_
