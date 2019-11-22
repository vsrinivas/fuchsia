// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SOURCE_FILE_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SOURCE_FILE_PROVIDER_H_

#include <ctime>
#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err_or.h"

namespace zxdb {

// Interface to provide source code. The default implementation fails for all requests. See
// SourceFileProviderImpl.
class SourceFileProvider {
 public:
  struct FileData {
    FileData() = default;
    FileData(std::string c, std::string path, std::time_t mtime)
        : contents(std::move(c)), full_path(std::move(path)), modification_time(mtime) {}

    std::string contents;

    // Resolved file path. This will be concatenated with the search path. If the search path
    // is system-absolute, this path will be, but if the search path is relative to the current
    // working directory, so will this be.
    std::string full_path;

    std::time_t modification_time = 0;
  };

  virtual ~SourceFileProvider() = default;

  // Attempts to read the contents of the given file. It is provided the file's build dir as
  // reported by the symbols (for in-tree-built files, this is not useful).
  virtual ErrOr<FileData> GetFileData(const std::string& file_name,
                                      const std::string& file_build_dir) const {
    return Err("Source not available.");
  }
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_SOURCE_FILE_PROVIDER_H_
