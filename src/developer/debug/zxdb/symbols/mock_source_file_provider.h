// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SOURCE_FILE_PROVIDER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SOURCE_FILE_PROVIDER_H_

#include <map>
#include <string>

#include "src/developer/debug/zxdb/symbols/source_file_provider.h"

namespace zxdb {

// Mock implementation of SourceFileProvider that returns canned input for known file names.
// No handling of paths is done and the file_build_dir is ignored: the file names must match
// exactly.
class MockSourceFileProvider : public SourceFileProvider {
 public:
  // Sets the expected contents for the given file.
  void SetFileData(const std::string& file_name, FileData data);

  ErrOr<FileData> GetFileData(const std::string& file_name,
                              const std::string& file_build_dir) const override;

 private:
  // Maps file names to their hardcoded contents.
  std::map<std::string, FileData> contents_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_MOCK_SOURCE_FILE_PROVIDER_H_
