// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_LOGGING_LOGGING_H_
#define SRC_DEVELOPER_DEBUG_SHARED_LOGGING_LOGGING_H_

// This header is meant to be the hub of debug logging: timers, logging, etc. There is no need to
// include the other headers directly.

#include <sstream>

#include "src/developer/debug/shared/logging/debug.h"
#include "src/developer/debug/shared/logging/file_line_function.h"
#include "src/developer/debug/shared/logging/macros.h"

namespace debug {

// Normally you would use this macro to create logging statements.
// Example:
//
// DEBUG_LOG(Job) << "Some job statement.";
// DEBUG_LOG(MessageLoop) << "Some event with id " << id;
//
// If the logging will occur on some other function, you can pass the location into the other macro:
//
//
// if (err.has_error())
//   LogSomewhereElse(FROM_HERE, LogCategory::kAgent, error.msg());
//
//  ...
//
// void LogSomewhereElse(FileLineFunction location, LogCategory category, std::string msg) {
//    ...
//    DEBUG_LOG_WITH_LOCATION(category, location) << msg;
// }
//

#define DEBUG_LOG_WITH_LOCATION(category, location)                                          \
  ::debug::LogStatement STRINGIFY(__debug_log, __LINE__)(location,                           \
                                                         ::debug::LogCategory::k##category); \
  STRINGIFY(__debug_log, __LINE__).stream()

#define DEBUG_LOG(category) DEBUG_LOG_WITH_LOCATION(category, FROM_HERE)

// Creates a conditional logger depending whether the debug mode is active or not. See debug.h for
// more details.
class LogStatement {
 public:
  explicit LogStatement(FileLineFunction origin, LogCategory);
  ~LogStatement();

  std::ostream& stream() { return stream_; }
  std::string GetMsg();

  const FileLineFunction& origin() const { return origin_; }
  LogCategory category() const { return category_; }
  double start_time() const { return start_time_; }

 private:
  FileLineFunction origin_;
  LogCategory category_;
  bool should_log_ = false;
  double start_time_;

  std::ostringstream stream_;
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_LOGGING_LOGGING_H_
