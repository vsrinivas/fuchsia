// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_GET_PAGE_ENSURE_INITIALIZED_H_
#define PERIDOT_BIN_LEDGER_TESTING_GET_PAGE_ENSURE_INITIALIZED_H_

#include <lib/fit/function.h>

#include "peridot/bin/ledger/fidl/include/types.h"

namespace ledger {
// Determines whether calling the |GetPageEnsureInitialized| callback should be
// done after some delay. This can be used in benchmarks, to make sure that all
// backround I/O operations have finished before measurements start.
enum DelayCallback : bool { NO, YES };

// Retrieves the requested page of the given Ledger instance amd returns after
// ensuring that it is initialized. If |id| is nullptr, a new page with a unique
// id is created.
void GetPageEnsureInitialized(
    LedgerPtr* ledger, PageIdPtr requested_id, DelayCallback delay_callback,
    fit::function<void()> error_handler,
    fit::function<void(Status, PagePtr, PageId)> callback);
}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_GET_PAGE_ENSURE_INITIALIZED_H_
