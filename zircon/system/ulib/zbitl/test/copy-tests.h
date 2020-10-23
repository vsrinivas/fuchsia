// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_COPY_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_COPY_TESTS_H_

#include <zircon/assert.h>

#include <sstream>
#include <string>

// An error message for an error returned by the Copy API.
template <typename CopyError>
std::string CopyResultErrorMsg(CopyError copy_error) {
  std::stringstream ss;
  auto append = [&ss](auto&& io_error) {
    if constexpr (std::is_integral_v<decltype(io_error)>) {
      ss << " " << io_error << "\n";
    }
  };
  ss << copy_error.zbi_error;
  if (copy_error.read_error) {
    ss << ": read error at offset " << std::hex << copy_error.read_offset;
    append(copy_error.read_error.value());
  } else if (copy_error.write_error) {
    ss << ": write error at offset " << std::hex << copy_error.write_offset;
    append(copy_error.write_error.value());
  }
  return ss.str();
}

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_COPY_TESTS_H_
