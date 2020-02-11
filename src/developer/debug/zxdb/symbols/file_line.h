// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_FILE_LINE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_FILE_LINE_H_

#include <string>

namespace zxdb {

class FileLine {
 public:
  FileLine();

  // Constructor for a file/line with no compilation directory.
  FileLine(std::string file, int line);

  // Constructor with a compilation directory. comp_dir may be empty if not known.
  FileLine(std::string file, std::string comp_dir, int line);

  ~FileLine();

  bool is_valid() const { return !file_.empty() && line_ > 0; }

  // In our system the file name is always the string that comes out of DWARF which is relative to
  // the compilation directory.
  const std::string& file() const { return file_; }

  // The compilation directory from the symbol file that contained the file name. This can have
  // different meanings depending on compilation options. It can be the absolute path on the system
  // that did the compilation of the file.
  //
  // It can also be empty or some relative directory, or it can be an invalid directory if the
  // build happened on another computer.
  //
  // Because the meaning of this is impossible to know in advance, it's split out so the outer
  // code can interpret the file based on settings or by trying to find the file in different ways.
  const std::string& comp_dir() const { return comp_dir_; }

  int line() const { return line_; }

 private:
  std::string file_;
  std::string comp_dir_;
  int line_ = 0;
};

// Comparison function for use in set and map.
bool operator<(const FileLine& a, const FileLine& b);

bool operator==(const FileLine& a, const FileLine& b);
bool operator!=(const FileLine& a, const FileLine& b);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_FILE_LINE_H_
