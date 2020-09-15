// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_CONSOLE_ERR_H_
#define SRC_DEVELOPER_SHELL_CONSOLE_ERR_H_

#include <zircon/types.h>

#include <string>

namespace shell::console {

// zx_status_t is for the negative numbers, and ErrorType is for the positive ones.
enum ErrorType : zx_status_t { kNone, kBadParse };

class Err {
 public:
  Err() : msg(""), code(kNone) {}
  Err(zx_status_t code, std::string msg) : msg(std::move(msg)), code(code) {}
  bool ok() const { return code == kNone; }

  std::string msg;
  zx_status_t code = kNone;
};

}  // namespace shell::console

#endif  // SRC_DEVELOPER_SHELL_CONSOLE_ERR_H_
