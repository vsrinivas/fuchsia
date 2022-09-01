// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_LOGGING_FILE_LINE_FUNCTION_H_
#define SRC_DEVELOPER_DEBUG_SHARED_LOGGING_FILE_LINE_FUNCTION_H_

#include <string>

namespace debug {

#define FROM_HERE ::debug::FileLineFunction(__FILE__, __LINE__, __FUNCTION__)
#define FROM_HERE_NO_FUNC ::debug::FileLineFunction(__FILE__, __LINE__)

// For performance, this class accepts "const char*" instead of "std::string" for file and function
// names. It's usually not an issue since __FILE__ and __FUNCTION__ macros are generating static,
// global strings. However, if you construct |FileLineFunction| from a temporary string, i.e.,
// "std::string().c_str()", the lifecycle of the |FileLineFunction| object must be taken care of.
class FileLineFunction {
 public:
  FileLineFunction() = default;
  FileLineFunction(const char* file, uint32_t line, const char* function = nullptr)
      : file_(file), function_(function), line_(line) {}

  bool is_valid() const { return file_ && line_ > 0; }

  const char* file() const { return file_; }
  uint32_t line() const { return line_; }
  const char* function() const { return function_; }

  std::string ToString() const;

 private:
  const char* file_ = nullptr;
  const char* function_ = nullptr;
  uint32_t line_ = 0;
};

// Comparison function for use in set and map.
bool operator<(const FileLineFunction& a, const FileLineFunction& b);

bool operator==(const FileLineFunction& a, const FileLineFunction& b);
bool operator!=(const FileLineFunction& a, const FileLineFunction& b);

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_LOGGING_FILE_LINE_FUNCTION_H_
