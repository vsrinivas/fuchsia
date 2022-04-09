// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TYPES_H_
#define SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TYPES_H_

#include <zircon/types.h>

#include <cstdint>
#include <iostream>
#include <string_view>

namespace zxdump {

using ByteView = std::basic_string_view<std::byte>;

// fitx::result<zxdump::Error> is used as the return type of many operations.
// It carries a zx_status_t and a string describing what operation failed.
struct Error {
  std::string_view status_string() const;

  std::string_view op_;
  zx_status_t status_{};
};

}  // namespace zxdump

// This prints "op: status" with the status string.
std::ostream& operator<<(std::ostream& os, const zxdump::Error& error);

#endif  // SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TYPES_H_
