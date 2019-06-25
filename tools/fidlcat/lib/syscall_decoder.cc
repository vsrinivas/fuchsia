// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidlcat/lib/syscall_decoder.h"

#include <zircon/system/public/zircon/types.h>

#include <cstdint>
#include <iostream>

namespace fidlcat {

#define ErrorNameCase(name) \
  case name:                \
    os << #name;            \
    return

// TODO: (use zx_status_get_string when it will be available).
void ErrorName(int64_t error_code, std::ostream& os) {
  switch (error_code) {
    ErrorNameCase(ZX_ERR_INTERNAL);
    ErrorNameCase(ZX_ERR_NOT_SUPPORTED);
    ErrorNameCase(ZX_ERR_NO_RESOURCES);
    ErrorNameCase(ZX_ERR_NO_MEMORY);
    ErrorNameCase(ZX_ERR_INTERNAL_INTR_RETRY);
    ErrorNameCase(ZX_ERR_INVALID_ARGS);
    ErrorNameCase(ZX_ERR_BAD_HANDLE);
    ErrorNameCase(ZX_ERR_WRONG_TYPE);
    ErrorNameCase(ZX_ERR_BAD_SYSCALL);
    ErrorNameCase(ZX_ERR_OUT_OF_RANGE);
    ErrorNameCase(ZX_ERR_BUFFER_TOO_SMALL);
    ErrorNameCase(ZX_ERR_BAD_STATE);
    ErrorNameCase(ZX_ERR_TIMED_OUT);
    ErrorNameCase(ZX_ERR_SHOULD_WAIT);
    ErrorNameCase(ZX_ERR_CANCELED);
    ErrorNameCase(ZX_ERR_PEER_CLOSED);
    ErrorNameCase(ZX_ERR_NOT_FOUND);
    ErrorNameCase(ZX_ERR_ALREADY_EXISTS);
    ErrorNameCase(ZX_ERR_ALREADY_BOUND);
    ErrorNameCase(ZX_ERR_UNAVAILABLE);
    ErrorNameCase(ZX_ERR_ACCESS_DENIED);
    ErrorNameCase(ZX_ERR_IO);
    ErrorNameCase(ZX_ERR_IO_REFUSED);
    ErrorNameCase(ZX_ERR_IO_DATA_INTEGRITY);
    ErrorNameCase(ZX_ERR_IO_DATA_LOSS);
    ErrorNameCase(ZX_ERR_IO_NOT_PRESENT);
    ErrorNameCase(ZX_ERR_IO_OVERRUN);
    ErrorNameCase(ZX_ERR_IO_MISSED_DEADLINE);
    ErrorNameCase(ZX_ERR_IO_INVALID);
    ErrorNameCase(ZX_ERR_BAD_PATH);
    ErrorNameCase(ZX_ERR_NOT_DIR);
    ErrorNameCase(ZX_ERR_NOT_FILE);
    ErrorNameCase(ZX_ERR_FILE_BIG);
    ErrorNameCase(ZX_ERR_NO_SPACE);
    ErrorNameCase(ZX_ERR_NOT_EMPTY);
    ErrorNameCase(ZX_ERR_STOP);
    ErrorNameCase(ZX_ERR_NEXT);
    ErrorNameCase(ZX_ERR_ASYNC);
    ErrorNameCase(ZX_ERR_PROTOCOL_NOT_SUPPORTED);
    ErrorNameCase(ZX_ERR_ADDRESS_UNREACHABLE);
    ErrorNameCase(ZX_ERR_ADDRESS_IN_USE);
    ErrorNameCase(ZX_ERR_NOT_CONNECTED);
    ErrorNameCase(ZX_ERR_CONNECTION_REFUSED);
    ErrorNameCase(ZX_ERR_CONNECTION_RESET);
    ErrorNameCase(ZX_ERR_CONNECTION_ABORTED);
    default:
      os << "errno=" << error_code;
      return;
  }
}

}  // namespace fidlcat
