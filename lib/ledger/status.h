// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_LIB_LEDGER_STATUS_H_
#define APPS_MODULAR_LIB_LEDGER_STATUS_H_

#include <string>

#include "lib/ledger/fidl/ledger.fidl.h"

namespace modular {

std::string LedgerStatusToString(ledger::Status status);

}  // namespace modular

#endif  // APPS_MODULAR_LIB_LEDGER_STATUS_H_
