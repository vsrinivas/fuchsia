// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTABLE_PAGE_H_
#define SRC_LEDGER_BIN_APP_INSPECTABLE_PAGE_H_

#include <lib/fit/function.h>

#include "src/ledger/bin/app/active_page_manager.h"
#include "src/ledger/bin/app/types.h"
#include "src/ledger/bin/storage/public/types.h"

namespace ledger {

// A helper interface implemented by |PageManager| with which Ledger's page-scoped,
// |PageManager|-owned Inspect-servicing classes conduct page-scoped operations.
class InspectablePage {
 public:
  virtual ~InspectablePage() = default;

  // Conduct some Inspect-related operation with the given |ActivePageManager|.
  virtual void NewInspection(fit::function<void(storage::Status status, ExpiringToken token,
                                                ActivePageManager* active_page_manager)>
                                 callback) = 0;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTABLE_PAGE_H_
