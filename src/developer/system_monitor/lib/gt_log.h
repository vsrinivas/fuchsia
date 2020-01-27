// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_SYSTEM_MONITOR_GT_LOG_H_
#define GARNET_LIB_SYSTEM_MONITOR_GT_LOG_H_

#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>

// TODO(sm_bug.com/48): This is a minimum logging system intended to quickly
// replace a prior logging API. This should be expanded over time.

// Each level will be tagged in the output. Output from levels can be enabled or
// disabled by ordinal value.
// This is defined outside of the namespace for user ergonomics.
enum class GuiToolsLogLevel : int {
  DEBUG3 = -3,
  DEBUG2 = -2,
  DEBUG = -1,
  INFO = 0,
  WARNING = 1,
  ERROR = 2,
  FATAL = 3,
};

namespace gt {

extern GuiToolsLogLevel g_log_level;
extern std::ofstream g_no_output_ofstream;

class Logger {
 public:
  // Write the line prefix.
  // Note: |out| and |file_path| must not be null.
  Logger(std::ostream* out, GuiToolsLogLevel level, GuiToolsLogLevel limit,
         const char* file_path, int line, const char* function)
      : out_(out) {
    assert(out != nullptr);
    assert(file_path != nullptr);
    if (level < limit) {
      out_ = &g_no_output_ofstream;
      return;
    }
    switch (level) {
      case GuiToolsLogLevel::FATAL:
        *out_ << "[FATAL]";
        break;
      case GuiToolsLogLevel::ERROR:
        *out_ << "[ERROR]";
        break;
      case GuiToolsLogLevel::WARNING:
        *out_ << "[WARNING]";
        break;
      case GuiToolsLogLevel::INFO:
        *out_ << "[INFO]";
        break;
      case GuiToolsLogLevel::DEBUG:
        *out_ << "[DEBUG]";
        break;
      case GuiToolsLogLevel::DEBUG2:
        *out_ << "[DEBUG2]";
        break;
      case GuiToolsLogLevel::DEBUG3:
        *out_ << "[DEBUG3]";
        break;
      default:
        *out_ << "[UNKNOWN]";
        break;
    }
    *out_ << NameOnly(file_path) << ":" << line << ": " << function << ": ";
  }
  // Put an end-line on the output.
  ~Logger() { *out_ << std::endl; }

  // A stream for the log output for the caller to use.
  std::ostream& out() { return *out_; }

 private:
  std::ostream* out_;

  // Clip the name off the path. (It's too noisy to print a long file path on
  // each log line.)
  const char* NameOnly(const char* file_path) {
    const char* name = strrchr(file_path, '/');
    return name ? name + 1 : file_path;
  }
};

// Initialize the logging systems. The parameters are similar to those passed to
// main().
// Returns true on success.
bool SetUpLogging(int argc, const char* const* argv);

// Provides an ostream to send output to. Avoid calling this function directly.
// Use the GT_LOG macro provided below.
inline std::ostream& GuiToolsLog(GuiToolsLogLevel level) { return std::cout; }

}  // namespace gt.

// Use like you would `std::cout`. E.g.
// GT_LOG(INFO) << "The special value is " << special_value;
//
// A new-line will end each call implicitly. (If std::endl is passed, there will
// be two '\n' printed.)
#define GT_LOG(x)                                                            \
  ::gt::Logger(&std::cout, GuiToolsLogLevel::x, ::gt::g_log_level, __FILE__, \
               __LINE__, __FUNCTION__)                                       \
      .out()

#endif  // GARNET_LIB_SYSTEM_MONITOR_GT_LOG_H_
