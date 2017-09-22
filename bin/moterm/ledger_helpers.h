// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOTERM_LEDGER_HELPERS_H_
#define APPS_MOTERM_LEDGER_HELPERS_H_

#include <functional>
#include <string>

#include "lib/ledger/fidl/ledger.fidl.h"

namespace moterm {

void LogLedgerError(ledger::Status status, const std::string& description);

std::function<void(ledger::Status)> LogLedgerErrorCallback(
    std::string description);

}  // namespace moterm

#endif  // APPS_MOTERM_LEDGER_HELPERS_H_
