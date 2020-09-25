// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_PROGRAM_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_PROGRAM_H_

#include <fuchsia/data/llcpp/fidl.h>
#include <lib/zx/status.h>

inline zx::status<std::string> program_value(const llcpp::fuchsia::data::Dictionary& program,
                                             std::string_view key) {
  if (program.has_entries()) {
    for (auto& entry : program.entries()) {
      if (!std::equal(key.begin(), key.end(), entry.key.begin())) {
        continue;
      }
      if (!entry.value.is_str()) {
        return zx::error(ZX_ERR_WRONG_TYPE);
      }
      auto& value = entry.value.str();
      return zx::ok(std::string{value.data(), value.size()});
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_PROGRAM_H_
