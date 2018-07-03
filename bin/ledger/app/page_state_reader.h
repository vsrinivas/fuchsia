// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_PAGE_STATE_READER_H_
#define PERIDOT_BIN_LEDGER_APP_PAGE_STATE_READER_H_

#include <functional>

#include <lib/fxl/strings/string_view.h>

#include "peridot/bin/ledger/app/types.h"
#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

// An observer of the state of a Page.
class PageStateReader {
 public:
  // Checks whether the given page is closed and synced. The result returned in
  // the callback will be |PageClosedAndSynced:UNKNOWN| if the page is opened
  // after calling this method and before the callback is called. Otherwise it
  // will be |YES| or |NO| depending on whether the page is synced.
  virtual void PageIsClosedAndSynced(
      fxl::StringView ledger_name, storage::PageIdView page_id,
      fit::function<void(Status, PageClosedAndSynced)> callback) = 0;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_PAGE_STATE_READER_H_
