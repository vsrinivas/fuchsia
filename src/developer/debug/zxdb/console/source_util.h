// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_SOURCE_UTIL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_SOURCE_UTIL_H_

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err_or.h"

namespace zxdb {

class SettingStore;

// Interface to provide source code. The default implementation fails for all requests. See
// SourceFileProviderImpl.
class SourceFileProvider {
 public:
  virtual ~SourceFileProvider() = default;

  // Attempts to read the contents of the given file. It is provided the file's build dir as
  // reported by the symbols (for in-tree-built files, this is not useful).
  virtual ErrOr<std::string> GetFileContents(const std::string& file_name,
                                             const std::string& file_build_dir) const {
    return Err("Source not available.");
  }
};

// Implementation of SourceFileProvider that searches the local disk. It uses the build directory
// preferences from the SettingStore to search in.
class SourceFileProviderImpl : public SourceFileProvider {
 public:
  explicit SourceFileProviderImpl(std::vector<std::string> build_dirs);
  explicit SourceFileProviderImpl(const SettingStore& settings);

  ErrOr<std::string> GetFileContents(const std::string& file_name,
                                     const std::string& file_build_dir) const override;

 private:
  const std::vector<std::string> build_dir_prefs_;
};

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
