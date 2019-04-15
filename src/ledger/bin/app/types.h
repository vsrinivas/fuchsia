// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_TYPES_H_
#define SRC_LEDGER_BIN_APP_TYPES_H_

#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/zx/time.h>

#include <string>

#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

// A token that performs a given action on destruction.
using ExpiringToken = fit::deferred_action<fit::closure>;

// The result of a predicate, meant to be checked on a closed page. The result
// is |YES| or |NO| depending on whether the predicate is satisfied or not. If
// however the page was opened during the operation, |PAGE_OPENED| is returned.
enum class PagePredicateResult { YES, NO, PAGE_OPENED };

// Holds information on when a page was last used.
struct PageInfo {
  //  The timestamp used for all currently opened pages.
  static constexpr zx::time_utc kOpenedPageTimestamp =
      zx::time_utc::infinite_past();

  std::string ledger_name;
  storage::PageId page_id;
  // The timestamp in UTC of when the page was last closed, as an indication
  // of when it was last used. If the page is currently open, the value is set
  // to |PageInfo::kOpenedPageTimestamp|.
  zx::time_utc timestamp;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_TYPES_H_
