// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sstream>

#include "src/developer/debug/ipc/debug/file_line_function.h"

namespace debug_ipc {

// Creates a conditional logger depending whether the debug mode is active or
// not. See debug.h for more details.

class LogStatement {
 public:
  explicit LogStatement(FileLineFunction);
  ~LogStatement();

  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
  FileLineFunction location_;
};

#define DEBUG_LOG() ::debug_ipc::LogStatement(FROM_HERE).stream()

}  // namespace debug_ipc
