// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_AVAILABILITY_MANAGER_H_
#define SRC_LEDGER_BIN_APP_PAGE_AVAILABILITY_MANAGER_H_

#include <lib/fit/function.h>

#include <optional>
#include <vector>

namespace ledger {

// Stores whether a page is busy or available. After |MarkPageBusy| has been called, all calls to
// |OnPageAvailable| will be delayed until a call to |MarkPageAvailable|. By default the page is
// available.
class PageAvailabilityManager {
 public:
  // Marks the page as busy and delays calling the callback in |OnPageAvailable| for the page. It is
  // an error to call this method when the page is already busy.
  void MarkPageBusy();

  // Marks the page as available and calls any pending callbacks from |OnPageAvailable| for this
  // page. It is an error to call this method when the page is already available.
  void MarkPageAvailable();

  // If the page is available calls the given callback directly. Otherwise,
  // the callback is registered util the page becomes available.
  void OnPageAvailable(fit::closure on_page_available);

  // Checks whether the page is available.
  bool IsDiscardable() const;

  void SetOnDiscardable(fit::closure on_discardable);

 private:
  // Checks if the object is empty; if it is empty and on_discardable_
  // is set, calls on_discardable_.
  void CheckDiscardable();

  // Stores the pending callbacks while the page is busy.
  std::optional<std::vector<fit::closure>> on_available_callbacks_;

  fit::closure on_discardable_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_AVAILABILITY_MANAGER_H_
