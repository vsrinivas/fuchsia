// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_
#define SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_

#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

// A listener on page usage, that receives notifications when a page is  opened
// or closed.
class PageUsageListener {
 public:
  PageUsageListener() {}
  virtual ~PageUsageListener() {}

  // Called when a page connection has been requested. In case of concurrent
  // connections to the same page, this should only be called once, on the first
  // connection.
  virtual void OnPageOpened(fxl::StringView ledger_name, storage::PageIdView page_id) = 0;

  // Called when all external connections to a page are closed. In case of
  // concurrent connections to the same page, this should only be called once,
  // when the last connection closes.
  // TODO(nellyv): Add argument on whether the page is synced and and cache it.
  virtual void OnPageClosed(fxl::StringView ledger_name, storage::PageIdView page_id) = 0;

  // Called when there are no longer any active connections to a page. This
  // includes both internal and external connections. Note that if the last
  // active connection to a page is an external one, both |OnPageClosed| and
  // then |OnPageUnused| will be called.
  virtual void OnPageUnused(fxl::StringView ledger_name, storage::PageIdView page_id) = 0;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(PageUsageListener);
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_
