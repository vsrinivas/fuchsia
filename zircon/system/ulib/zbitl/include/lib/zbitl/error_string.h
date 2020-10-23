// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSDstyle license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_ERROR_STRING_H_
#define LIB_ZBITL_ERROR_STRING_H_

#include <string>

// The format of the error strings below should be kept in sync with that of
// the printed messages in error_stdio.h.

namespace zbitl {

// Returns an error string from a View `Error` value.
template <typename ViewError>
std::string ViewErrorString(const ViewError& error) {
  std::string s{error.zbi_error};
  s += "at offset ";
  s += std::to_string(error.item_offset);
  if (error.storage_error) {
    s += ": ";
    s += ViewError::storage_error_string(error.storage_error.value());
  }
  return s;
}

// Returns an error string from a View `CopyError` value.
template <typename ViewCopyError>
std::string ViewCopyErrorString(const ViewCopyError& error) {
  std::string s{error.zbi_error};
  if (error.read_error) {
    s += ": read error at source offset ";
    s += std::to_string(error.read_offset);
    s += ": ";
    s += ViewCopyError::read_error_string(error.read_error.value());
  } else if (error.write_error) {
    s += ": write error at destination offset ";
    s += std::to_string(error.write_offset);
    s += ": ";
    s += ViewCopyError::write_error_string(error.write_error.value());
  }
  return s;
}

}  // namespace zbitl

#endif  // LIB_ZBITL_ERROR_STRING_H_
