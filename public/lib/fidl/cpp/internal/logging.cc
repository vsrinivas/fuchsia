// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/logging.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

namespace fidl {
namespace internal {

void ReportEncodingError(const Message& message,
                         const fidl_type_t* type,
                         const char* error_msg,
                         const char* file,
                         int line) {
  fprintf(stderr,
          "fidl encoding error at %s:%d: %s, "
          "%" PRIu32 " bytes, %" PRIu32 " handles\n",
          file, line, error_msg, message.bytes().actual(),
          message.handles().actual());
}

void ReportDecodingError(const Message& message,
                         const fidl_type_t* type,
                         const char* error_msg,
                         const char* file,
                         int line) {
  fprintf(stderr,
          "fidl decoding error at %s:%d: %s, "
          "%" PRIu32 " bytes, %" PRIu32 " handles\n",
          file, line, error_msg, message.bytes().actual(),
          message.handles().actual());
}

}  // namespace internal
}  // namespace fidl
