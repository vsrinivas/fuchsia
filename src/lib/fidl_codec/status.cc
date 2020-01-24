
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fidl_codec/status.h"

#include <sstream>

namespace fidl_codec {

#define StatusNameCase(name) \
  case name:                 \
    return #name;

std::string StatusName(zx_status_t status) {
  switch (status) {
    StatusNameCase(ZX_OK);
    StatusNameCase(ZX_ERR_INTERNAL);
    StatusNameCase(ZX_ERR_NOT_SUPPORTED);
    StatusNameCase(ZX_ERR_NO_RESOURCES);
    StatusNameCase(ZX_ERR_NO_MEMORY);
    StatusNameCase(ZX_ERR_INTERNAL_INTR_RETRY);
    StatusNameCase(ZX_ERR_INVALID_ARGS);
    StatusNameCase(ZX_ERR_BAD_HANDLE);
    StatusNameCase(ZX_ERR_WRONG_TYPE);
    StatusNameCase(ZX_ERR_BAD_SYSCALL);
    StatusNameCase(ZX_ERR_OUT_OF_RANGE);
    StatusNameCase(ZX_ERR_BUFFER_TOO_SMALL);
    StatusNameCase(ZX_ERR_BAD_STATE);
    StatusNameCase(ZX_ERR_TIMED_OUT);
    StatusNameCase(ZX_ERR_SHOULD_WAIT);
    StatusNameCase(ZX_ERR_CANCELED);
    StatusNameCase(ZX_ERR_PEER_CLOSED);
    StatusNameCase(ZX_ERR_NOT_FOUND);
    StatusNameCase(ZX_ERR_ALREADY_EXISTS);
    StatusNameCase(ZX_ERR_ALREADY_BOUND);
    StatusNameCase(ZX_ERR_UNAVAILABLE);
    StatusNameCase(ZX_ERR_ACCESS_DENIED);
    StatusNameCase(ZX_ERR_IO);
    StatusNameCase(ZX_ERR_IO_REFUSED);
    StatusNameCase(ZX_ERR_IO_DATA_INTEGRITY);
    StatusNameCase(ZX_ERR_IO_DATA_LOSS);
    StatusNameCase(ZX_ERR_IO_NOT_PRESENT);
    StatusNameCase(ZX_ERR_IO_OVERRUN);
    StatusNameCase(ZX_ERR_IO_MISSED_DEADLINE);
    StatusNameCase(ZX_ERR_IO_INVALID);
    StatusNameCase(ZX_ERR_BAD_PATH);
    StatusNameCase(ZX_ERR_NOT_DIR);
    StatusNameCase(ZX_ERR_NOT_FILE);
    StatusNameCase(ZX_ERR_FILE_BIG);
    StatusNameCase(ZX_ERR_NO_SPACE);
    StatusNameCase(ZX_ERR_NOT_EMPTY);
    StatusNameCase(ZX_ERR_STOP);
    StatusNameCase(ZX_ERR_NEXT);
    StatusNameCase(ZX_ERR_ASYNC);
    StatusNameCase(ZX_ERR_PROTOCOL_NOT_SUPPORTED);
    StatusNameCase(ZX_ERR_ADDRESS_UNREACHABLE);
    StatusNameCase(ZX_ERR_ADDRESS_IN_USE);
    StatusNameCase(ZX_ERR_NOT_CONNECTED);
    StatusNameCase(ZX_ERR_CONNECTION_REFUSED);
    StatusNameCase(ZX_ERR_CONNECTION_RESET);
    StatusNameCase(ZX_ERR_CONNECTION_ABORTED);
    default: {
      std::stringstream os;
      os << "status=" << status;
      return os.str();
    }
  }
}

}  // namespace fidl_codec
