// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/logging.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

namespace fidl {
namespace internal {

void ReportEncodingError(const Message& message, const fidl_type_t* type,
                         const char* error_msg, const char* file, int line) {
  char type_name[1024];
  size_t type_name_length =
      fidl_format_type_name(type, type_name, sizeof(type_name));
  fprintf(stderr,
          "fidl encoding error at %s:%d: %s, "
          "type %.*s, %" PRIu32 " bytes, %" PRIu32 " handles\n",
          file, line, error_msg, static_cast<int>(type_name_length), type_name,
          message.bytes().actual(), message.handles().actual());
}

void ReportDecodingError(const Message& message, const fidl_type_t* type,
                         const char* error_msg, const char* file, int line) {
  char type_name[1024];
  size_t type_name_length =
      fidl_format_type_name(type, type_name, sizeof(type_name));
  fprintf(stderr,
          "fidl decoding error at %s:%d: %s, "
          "type %.*s, %" PRIu32 " bytes, %" PRIu32 " handles\n",
          file, line, error_msg, static_cast<int>(type_name_length), type_name,
          message.bytes().actual(), message.handles().actual());
}

void ReportChannelWritingError(const Message& message, const fidl_type_t* type,
                               zx_status_t status, const char* file, int line) {
  char type_name[1024];
  size_t type_name_length =
      fidl_format_type_name(type, type_name, sizeof(type_name));
  fprintf(stderr,
          "fidl channel writing error at %s:%d: zx_status_t %d, "
          "type %.*s, %" PRIu32 " bytes, %" PRIu32 " handles\n",
          file, line, status, static_cast<int>(type_name_length), type_name,
          message.bytes().actual(), message.handles().actual());
}

}  // namespace internal
}  // namespace fidl
