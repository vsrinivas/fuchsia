// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace debug_ipc {

#define FROM_HERE \
  ::debug_ipc::FileLineFunction(__FILE__, __LINE__, __FUNCTION__)
#define FROM_HERE_NO_FUNC ::debug_ipc::FileLineFunction(__FILE__, __LINE__)

class FileLineFunction {
 public:
  FileLineFunction();
  FileLineFunction(std::string file, int line, std::string function = "");
  ~FileLineFunction();

  bool is_valid() const { return !file_.empty() && line_ > 0; }

  const std::string& file() const { return file_; }
  int line() const { return line_; }

  std::string ToString() const;
  // Removes everything up the the filename from the file path.
  std::string ToStringWithBasename() const;

 private:
  std::string file_;
  std::string function_;
  int line_ = 0;
};

// Comparison function for use in set and map.
bool operator<(const FileLineFunction& a, const FileLineFunction& b);

bool operator==(const FileLineFunction& a, const FileLineFunction& b);
bool operator!=(const FileLineFunction& a, const FileLineFunction& b);

}  // namespace debug_ipc
