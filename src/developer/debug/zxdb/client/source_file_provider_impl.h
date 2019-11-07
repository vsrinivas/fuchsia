// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SOURCE_FILE_PROVIDER_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SOURCE_FILE_PROVIDER_IMPL_H_

#include <string>
#include <vector>

#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/symbols/source_file_provider.h"

namespace zxdb {

class SettingStore;

// Implementation of SourceFileProvider that searches the local disk. It uses the build directory
// preferences from the SettingStore to search in.
class SourceFileProviderImpl : public SourceFileProvider {
 public:
  explicit SourceFileProviderImpl(std::vector<std::string> build_dirs);
  explicit SourceFileProviderImpl(const SettingStore& settings);

  ErrOr<FileData> GetFileData(const std::string& file_name,
                              const std::string& file_build_dir) const override;

 private:
  const std::vector<std::string> build_dir_prefs_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SOURCE_FILE_PROVIDER_IMPL_H_
