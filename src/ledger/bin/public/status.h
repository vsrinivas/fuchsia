// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PUBLIC_STATUS_H_
#define SRC_LEDGER_BIN_PUBLIC_STATUS_H_

#include <zircon/status.h>

#include <ostream>

#include "src/lib/fxl/compiler_specific.h"
#include "src/lib/fxl/strings/string_view.h"

namespace ledger {

// Internal Status for the ledger codebase.
enum class FXL_WARN_UNUSED_RESULT Status {
  // Temporary status or status for tests. This is the first value as 0 is the
  // most probable value a non initialized variable will have.
  NOT_IMPLEMENTED = 0,

  // User visible status.
  OK,
  PAGE_NOT_FOUND,
  KEY_NOT_FOUND,
  NETWORK_ERROR,

  // Internal status.
  DATA_INTEGRITY_ERROR,
  ILLEGAL_STATE,
  INTERNAL_NOT_FOUND,
  INTERNAL_ERROR,
  INVALID_ARGUMENT,
  INTERRUPTED,
  IO_ERROR,
};

// Returns the string representation of |status|.
fxl::StringView StatusToString(Status status);

// Returns the |zx_status_t| equivalent for the given |status|. This is only
// valid for non user visible status.
zx_status_t ConvertToEpitaph(Status status);

// Outputs the string representation of |status| on |os|.
std::ostream& operator<<(std::ostream& os, Status status);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PUBLIC_STATUS_H_
