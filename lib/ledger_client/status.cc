// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/ledger_client/status.h"

#include <string>

#include <fuchsia/ledger/cpp/fidl.h>
#include <lib/fxl/logging.h>

namespace modular {

std::string LedgerStatusToString(fuchsia::ledger::Status status) {
  switch (status) {
    case fuchsia::ledger::Status::OK:
      return "OK";
    case fuchsia::ledger::Status::PARTIAL_RESULT:
      return "PARTIAL_RESULT";
    case fuchsia::ledger::Status::INVALID_TOKEN:
      return "INVALID_TOKEN";
    case fuchsia::ledger::Status::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case fuchsia::ledger::Status::PAGE_NOT_FOUND:
      return "PAGE_NOT_FOUND";
    case fuchsia::ledger::Status::KEY_NOT_FOUND:
      return "KEY_NOT_FOUND";
    case fuchsia::ledger::Status::REFERENCE_NOT_FOUND:
      return "REFERENCE_NOT_FOUND";
    case fuchsia::ledger::Status::NEEDS_FETCH:
      return "NEEDS_FETCH";
    case fuchsia::ledger::Status::IO_ERROR:
      return "IO_ERROR";
    case fuchsia::ledger::Status::NETWORK_ERROR:
      return "NETWORK_ERROR";
    case fuchsia::ledger::Status::TRANSACTION_ALREADY_IN_PROGRESS:
      return "TRANSACTION_ALREADY_IN_PROGRESS";
    case fuchsia::ledger::Status::NO_TRANSACTION_IN_PROGRESS:
      return "NO_TRANSACTION_IN_PROGRESS";
    case fuchsia::ledger::Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case fuchsia::ledger::Status::VALUE_TOO_LARGE:
      return "VALUE_TOO_LARGE";
    case fuchsia::ledger::Status::ILLEGAL_STATE:
      return "ILLEGAL_STATE";
    case fuchsia::ledger::Status::UNKNOWN_ERROR:
      return "UNKNOWN_ERROR";
  }
};

}  // namespace modular
