// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/ledger_client/status.h"

#include <string>

#include "lib/fxl/logging.h"
#include "lib/ledger/fidl/ledger.fidl.h"

namespace modular {

std::string LedgerStatusToString(ledger::Status status) {
  switch (status) {
    case ledger::Status::OK:
      return "OK";
    case ledger::Status::PARTIAL_RESULT:
      return "PARTIAL_RESULT";
    case ledger::Status::INVALID_TOKEN:
      return "INVALID_TOKEN";
    case ledger::Status::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case ledger::Status::PAGE_NOT_FOUND:
      return "PAGE_NOT_FOUND";
    case ledger::Status::KEY_NOT_FOUND:
      return "KEY_NOT_FOUND";
    case ledger::Status::REFERENCE_NOT_FOUND:
      return "REFERENCE_NOT_FOUND";
    case ledger::Status::NEEDS_FETCH:
      return "NEEDS_FETCH";
    case ledger::Status::IO_ERROR:
      return "IO_ERROR";
    case ledger::Status::NETWORK_ERROR:
      return "NETWORK_ERROR";
    case ledger::Status::TRANSACTION_ALREADY_IN_PROGRESS:
      return "TRANSACTION_ALREADY_IN_PROGRESS";
    case ledger::Status::NO_TRANSACTION_IN_PROGRESS:
      return "NO_TRANSACTION_IN_PROGRESS";
    case ledger::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case ledger::Status::KEY_TOO_LARGE:
      return "KEY_TOO_LARGE";
    case ledger::Status::VALUE_TOO_LARGE:
      return "VALUE_TOO_LARGE";
    case ledger::Status::UNKNOWN_ERROR:
      return "UNKNOWN_ERROR";
  }
};

}  // namespace modular
