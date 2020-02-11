// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#ifndef SRC_DEVELOPER_SHELL_MIRROR_COMMON_H_
#define SRC_DEVELOPER_SHELL_MIRROR_COMMON_H_

namespace shell::mirror {

// General purpose error used through the client and server.

enum ErrorType {
  kNone,
  kInit,
  kConnection,
  kWrite,
  kRead,
};

class Err {
 public:
  Err() : msg(""), code(kNone) {}
  Err(ErrorType code, std::string msg) : msg(std::move(msg)), code(code) {}
  bool ok() { return code == kNone; }

  std::string msg;
  ErrorType code = kNone;
};

}  // namespace shell::mirror

#endif  // SRC_DEVELOPER_SHELL_MIRROR_COMMON_H_
