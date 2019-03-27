// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

// Reads the source file from disk.
Err GetFileContents(const std::string& file_name, const std::string& build_dir,
                    std::string* contents);

// Extracts the given ranges of lines from the source contents. Line numbers are
// 1-based and inclusive. This may do short reads if the file isn't large
// enough. The first line must be at least 1 (sort reads can't work off the
// beginning since the caller won't know what the first line is).
std::vector<std::string> ExtractSourceLines(const std::string& contents,
                                            int first_line, int last_line);

// Extracts all source lines.
std::vector<std::string> ExtractSourceLines(const std::string& contents);

}  // namespace zxdb
