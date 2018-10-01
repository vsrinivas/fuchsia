// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_LEDGER_CLIENT_STATUS_H_
#define PERIDOT_LIB_LEDGER_CLIENT_STATUS_H_

#include <string>

#include <fuchsia/ledger/cpp/fidl.h>

namespace modular {

std::string LedgerStatusToString(fuchsia::ledger::Status status);
std::string LedgerEpitaphToString(zx_status_t status);

}  // namespace modular

#endif  // PERIDOT_LIB_LEDGER_CLIENT_STATUS_H_
