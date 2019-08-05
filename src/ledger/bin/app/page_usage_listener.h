// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_
#define SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_

#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

// A listener on page usage, that receives notifications when a page is opened or closed by internal
// or external connetions.
class PageUsageListener {
 public:
  PageUsageListener() {}
  virtual ~PageUsageListener() {}

  // Called when an external page connection has been requested. In case of concurrent external
  // connections to the same page, this should only be called once, on the first connection.
  virtual void OnExternallyUsed(fxl::StringView ledger_name, storage::PageIdView page_id) = 0;

  // Called when the last open external connection to a page is closed.
  virtual void OnExternallyUnused(fxl::StringView ledger_name, storage::PageIdView page_id) = 0;

  // Called when an internal page connection has been requested. In case of concurrent internal
  // connections to the same page, this should only be called once, on the first connection.
  virtual void OnInternallyUsed(fxl::StringView ledger_name, storage::PageIdView page_id) = 0;

  // Called when the last open internal connection to a page is closed.
  virtual void OnInternallyUnused(fxl::StringView ledger_name, storage::PageIdView page_id) = 0;



 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageUsageListener);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_
