// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/mock_source_file_provider.h"

namespace zxdb {

void MockSourceFileProvider::SetFileData(const std::string& file_name, std::time_t mtime,
                                         std::string contents) {
  contents_[file_name] = FileData(std::move(contents), mtime);
}

ErrOr<SourceFileProvider::FileData> MockSourceFileProvider::GetFileData(
    const std::string& file_name, const std::string& file_build_dir) const {
  auto found = contents_.find(file_name);
  if (found == contents_.end())
    return Err("File not found: '%s'.", file_name.c_str());
  return found->second;
}

}  // namespace zxdb
