// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sstream>

#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/logging/file_line_function.h"

namespace debug_ipc {


// Normally you would use this macro to create logging statements.
// Example:
//
// DEBUG_LOG(Job) << "Some job statement.";
//
// DEBUG_LOG(MessageLoop) << "Some event with id " << id;
#define DEBUG_LOG(category)                                                   \
  ::debug_ipc::LogStatement(FROM_HERE, ::debug_ipc::LogCategory::k##category) \
      .stream()

// Creates a conditional logger depending whether the debug mode is active or
// not. See debug.h for more details.
class LogStatement {
 public:
  explicit LogStatement(FileLineFunction origin, LogCategory);
  ~LogStatement();

  std::ostream& stream() { return stream_; }

 private:
  FileLineFunction origin_;
  LogCategory category_;

  std::ostringstream stream_;
};

}  // namespace debug_ipc
