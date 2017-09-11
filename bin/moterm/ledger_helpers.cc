// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/moterm/ledger_helpers.h"

#include "lib/fxl/logging.h"

namespace moterm {

void LogLedgerError(ledger::Status status, const std::string& description) {
  if (status != ledger::Status::OK) {
    FXL_LOG(ERROR) << description << " failed, status: " << status;
  }
}

std::function<void(ledger::Status)> LogLedgerErrorCallback(
    std::string description) {
  return [description = std::move(description)](ledger::Status status) {
    LogLedgerError(status, description);
  };
}

}  // namespace moterm
