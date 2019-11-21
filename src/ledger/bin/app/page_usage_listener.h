// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_
#define SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_

#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

// A listener on page usage, that receives notifications when a page is opened or closed by internal
// or external connetions. Although OnUsed/OnUnused called will be balanced, an
// Unused call may be reordered after a Used.
class PageUsageListener {
 public:
  PageUsageListener() = default;
  PageUsageListener(const PageUsageListener&) = delete;
  PageUsageListener& operator=(const PageUsageListener&) = delete;
  virtual ~PageUsageListener() = default;

  // Called when an external page connection has been requested. In case of concurrent external
  // connections to the same page, this should only be called once, on the first connection.
  virtual void OnExternallyUsed(absl::string_view ledger_name, storage::PageIdView page_id) = 0;

  // Called when the last open external connection to a page is closed.
  virtual void OnExternallyUnused(absl::string_view ledger_name, storage::PageIdView page_id) = 0;

  // Called when an internal page connection has been requested. In case of concurrent internal
  // connections to the same page, this should only be called once, on the first connection.
  virtual void OnInternallyUsed(absl::string_view ledger_name, storage::PageIdView page_id) = 0;

  // Called when the last open internal connection to a page is closed.
  virtual void OnInternallyUnused(absl::string_view ledger_name, storage::PageIdView page_id) = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_PAGE_USAGE_LISTENER_H_
