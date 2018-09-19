// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_APP_TYPES_H_
#define PERIDOT_BIN_LEDGER_APP_TYPES_H_

#include <string>

#include <zx/time.h>

#include "peridot/bin/ledger/storage/public/types.h"

namespace ledger {

enum class YesNoUnknown { YES, NO, UNKNOWN };

using PageClosedAndSynced = YesNoUnknown;

using PageClosedOfflineAndEmpty = YesNoUnknown;

// Holds information on when a page was last used.
struct PageInfo {
  std::string ledger_name;
  storage::PageId page_id;
  // The timestamp in UTC of when the page was last closed, as an indication
  // of when it was last used. If the page is currently open, the value is set
  // to 0.
  zx::time_utc timestamp;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_APP_TYPES_H_
