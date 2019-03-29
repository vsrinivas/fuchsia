// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/source_util.h"

#include <limits>

#include "src/lib/fxl/logging.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/lib/files/file.h"

namespace zxdb {

Err GetFileContents(const std::string& file_name, const std::string& build_dir,
                    std::string* contents) {
  // Search for the source file. If it's relative, try to find it relative to
  // the build dir, and if that doesn't exist, try relative to the current
  // directory).
  if (IsPathAbsolute(file_name)) {
    // Absolute path, expect it to be readable or fail.
    if (!files::ReadFileToString(file_name, contents))
      return Err("Source file not found: " + file_name);
  } else if (!files::ReadFileToString(CatPathComponents(build_dir, file_name),
                                      contents)) {
    // Doesn't exist relative to build dir, fall back to trying to read it
    // from the current dir.
    if (!files::ReadFileToString(file_name, contents))
      return Err("Source file not found: " + file_name);
  }
  return Err();
}

std::vector<std::string> ExtractSourceLines(const std::string& contents,
                                            int first_line, int last_line) {
  FXL_DCHECK(first_line > 0);

  std::vector<std::string> result;

  constexpr char kCR = 13;
  constexpr char kLF = 10;

  int cur_line = 1;
  size_t line_begin = 0;  // Byte offset.
  while (line_begin < contents.size() && cur_line <= last_line) {
    size_t cur = line_begin;

    // Locate extent of current line.
    size_t next_line_begin = contents.size();
    size_t line_end = contents.size();
    while (cur < contents.size()) {
      if (contents[cur] == kCR) {
        // Either CR or CR+LF
        line_end = cur;
        if (cur < contents.size() - 1 && contents[cur + 1] == kLF) {
          next_line_begin = cur + 2;
        } else {
          next_line_begin = cur + 1;
        }
        break;
      } else if (contents[cur] == kLF) {
        // LF by itself.
        line_end = cur;
        next_line_begin = cur + 1;
        break;
      }
      cur++;
    }

    if (cur_line >= first_line && cur_line <= last_line)
      result.emplace_back(&contents[line_begin], line_end - line_begin);

    // Advance to next line.
    line_begin = next_line_begin;
    cur_line++;
  }

  return result;
}

std::vector<std::string> ExtractSourceLines(const std::string& contents) {
  return ExtractSourceLines(contents, 1, std::numeric_limits<int>::max());
}

}  // namespace zxdb
