// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_SOURCE_UTIL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_SOURCE_UTIL_H_

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

// Reads the source file_name from disk.
//
// This function applies the build directory searching logic. It is provided the file's build dir as
// reported by the symbols (for in-tree-built files, this is not useful), and the ordered
// preferences for the build directories. It will search for the files in these different locations.
Err GetFileContents(const std::string& file_name, const std::string& file_build_dir,
                    const std::vector<std::string>& build_dir_prefs, std::string* contents);

// Extracts the given ranges of lines from the source contents. Line numbers are
// 1-based and inclusive. This may do short reads if the file isn't large
// enough. The first line must be at least 1 (sort reads can't work off the
// beginning since the caller won't know what the first line is).
std::vector<std::string> ExtractSourceLines(const std::string& contents, int first_line,
                                            int last_line);

// Extracts all source lines.
std::vector<std::string> ExtractSourceLines(const std::string& contents);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_SOURCE_UTIL_H_
