// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_COPY_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_COPY_TESTS_H_

#include <string>

#include "src/lib/fxl/strings/string_printf.h"

// An error message for the return value of CopyRawItem() or
// CopyRawItemWithHeader().
template <typename CopyResult>
std::string CopyResultErrorMsg(CopyResult result) {
  std::string msg;
  if (result.is_error()) {
    auto input_error = std::move(result).error_value();
    msg = input_error.zbi_error.data();
    if (input_error.storage_error) {
      auto storage_error = std::move(input_error).storage_error;
      if constexpr (std::is_integral_v<decltype(storage_error)>) {
        fxl::StringAppendf(&msg, ": read error %d\n", storage_error);
      }
    }
  } else if (result.value().is_error()) {
    auto storage_error = std::move(result).value().error_value();
    if constexpr (std::is_integral_v<decltype(storage_error)>) {
      fxl::StringAppendf(&msg, ": write error %d\n", storage_error);
    }
  }
  return msg;
}

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_COPY_TESTS_H_
