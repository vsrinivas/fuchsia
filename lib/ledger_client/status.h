// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_LEDGER_CLIENT_STATUS_H_
#define PERIDOT_LIB_LEDGER_CLIENT_STATUS_H_

#include <string>

#include <ledger/cpp/fidl.h>

namespace fuchsia {
namespace modular {

std::string LedgerStatusToString(::ledger::Status status);

}  // namespace modular
}  // namespace fuchsia

#endif  // PERIDOT_LIB_LEDGER_CLIENT_STATUS_H_
