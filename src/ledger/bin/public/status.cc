// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/public/status.h"

#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace ledger {

absl::string_view StatusToString(Status status) {
  switch (status) {
    case Status::OK:
      return "OK";
    case Status::IO_ERROR:
      return "IO_ERROR";
    case Status::PAGE_NOT_FOUND:
      return "PAGE_NOT_FOUND";
    case Status::KEY_NOT_FOUND:
      return "KEY_NOT_FOUND";
    case Status::DATA_INTEGRITY_ERROR:
      return "DATA_INTEGRITY_ERROR";
    case Status::ILLEGAL_STATE:
      return "ILLEGAL_STATE";
    case Status::INTERNAL_NOT_FOUND:
      return "INTERNAL_NOT_FOUND";
    case Status::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case Status::INTERRUPTED:
      return "INTERRUPTED";
    case Status::CANCELED:
      return "CANCELED";
    case Status::INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case Status::NETWORK_ERROR:
      return "NETWORK_ERROR";
    case Status::NOT_IMPLEMENTED:
      return "NOT_IMPLEMENTED";
  }
}

zx_status_t ConvertToEpitaph(Status status) {
  switch (status) {
    case Status::OK:
    case Status::PAGE_NOT_FOUND:
    case Status::KEY_NOT_FOUND:
    case Status::NETWORK_ERROR:
      FXL_DCHECK(false) << "Status: " << status
                        << " is a visible status and should not be sent as epitaph";
      return ZX_ERR_INTERNAL;
    case Status::INTERRUPTED:
    case Status::NOT_IMPLEMENTED:
      FXL_DCHECK(false) << "Status: " << status << " should never be sent to the client.";
      return ZX_ERR_INTERNAL;
    case Status::CANCELED:
      return ZX_ERR_CANCELED;
    case Status::DATA_INTEGRITY_ERROR:
      return ZX_ERR_IO_DATA_INTEGRITY;
    case Status::ILLEGAL_STATE:
      return ZX_ERR_BAD_STATE;
    case Status::INTERNAL_NOT_FOUND:
      return ZX_ERR_NOT_FOUND;
    case Status::INVALID_ARGUMENT:
      return ZX_ERR_INVALID_ARGS;
    case Status::INTERNAL_ERROR:
      return ZX_ERR_INTERNAL;
    case Status::IO_ERROR:
      return ZX_ERR_IO;
  }
}

std::ostream& operator<<(std::ostream& os, Status status) { return os << StatusToString(status); }

}  // namespace ledger
