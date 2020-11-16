// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_ERROR_STDIO_H_
#define LIB_ZBITL_ERROR_STDIO_H_

#include <stdio.h>

#include <string_view>

// The format of the error messages below should be kept in sync with that of
// the returned strings in error_string.h.

namespace zbitl {

// Prints an error message from a View `Error` value.
template <typename ViewError>
void PrintViewError(const ViewError& error, FILE* f = stdout) {
  fprintf(f, "%.*s at offset %u", static_cast<int>(error.zbi_error.size()), error.zbi_error.data(),
          error.item_offset);
  if (error.storage_error) {
    auto storage_error = ViewError::storage_error_string(error.storage_error.value());
    std::string_view storage_error_sv = storage_error;
    fprintf(f, ": %.*s", static_cast<int>(storage_error_sv.size()), storage_error_sv.data());
  }
  fprintf(f, "\n");  // To flush the buffer.
}

// Prints an error message from a View `CopyError` value.
template <typename ViewCopyError>
void PrintViewCopyError(const ViewCopyError& error, FILE* f = stdout) {
  fprintf(f, "%.*s", static_cast<int>(error.zbi_error.size()), error.zbi_error.data());
  if (error.read_error) {
    auto read_error = ViewCopyError::read_error_string(error.read_error.value());
    std::string_view read_error_sv = read_error;
    fprintf(f, ": read error at source offset %u: %.*s", error.read_offset,
            static_cast<int>(read_error_sv.size()), read_error_sv.data());
  } else if (error.write_error) {
    auto write_error = ViewCopyError::write_error_string(error.write_error.value());
    std::string_view write_error_sv = write_error;
    fprintf(f, ": write error at destination offset %u: %.*s", error.write_offset,
            static_cast<int>(write_error_sv.size()), write_error_sv.data());
  }
  fprintf(f, "\n");  // To flush the buffer.
}

}  // namespace zbitl

#endif  // LIB_ZBITL_ERROR_STDIO_H_
