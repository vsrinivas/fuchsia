// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

class Symbol {
 public:
  Symbol() {}
  Symbol(const std::string& file, const std::string& function,
         int line, int column, int start_line)
      : valid_(true), file_(file), function_(function), line_(line),
        column_(column), start_line_(start_line) {}

  bool valid() const { return valid_; }

  const std::string& file() const { return file_; }
  const std::string& function() const { return function_; }
  int line() const { return line_; }
  int column() const { return column_; }
  int start_line() const { return start_line_; }

 private:
  bool valid_ = false;
  std::string file_;
  std::string function_;
  int line_ = 0;
  int column_ = 0;
  int start_line_ = 0;
};

}  // namespace zxdb
