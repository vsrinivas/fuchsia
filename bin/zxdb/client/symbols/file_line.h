// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

class FileLine {
 public:
  FileLine();
  FileLine(std::string file, int line);
  ~FileLine();

  bool is_valid() const { return !file_.empty() && line_ > 0; }

  const std::string& file() const { return file_; }
  int line() const { return line_; }

  // Returns the file name part of the path, which is the portion after the
  // last slash.
  std::string GetFileNamePart() const;

 private:
  std::string file_;
  int line_ = 0;
};

// Comparison function for use in set and map.
bool operator<(const FileLine& a, const FileLine& b);

}  // namespace zxdb
