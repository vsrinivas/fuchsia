// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"

#include <limits>

#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

SourceFileProviderImpl::SourceFileProviderImpl(std::vector<std::string> build_dirs)
    : build_dir_prefs_(std::move(build_dirs)) {}

SourceFileProviderImpl::SourceFileProviderImpl(const SettingStore& settings)
    : build_dir_prefs_(settings.GetList(ClientSettings::Target::kBuildDirs)) {}

ErrOr<SourceFileProvider::FileData> SourceFileProviderImpl::GetFileData(
    const std::string& file_name, const std::string& file_build_dir) const {
  std::string contents;

  // Search for the source file. If it's relative, try to find it relative to the build dir, and if
  // that doesn't exist, try relative to the current directory).
  if (IsPathAbsolute(file_name)) {
    // Absolute path, expect it to be readable or fail.
    if (files::ReadFileToString(file_name, &contents))
      return FileData(std::move(contents), file_name, GetFileModificationTime(file_name));
    return Err("Source file not found: " + file_name);
  }

  // Search the build directory preferences in order.
  for (const auto& cur : build_dir_prefs_) {
    std::string cur_path = CatPathComponents(cur, file_name);
    if (files::ReadFileToString(cur_path, &contents))
      return FileData(std::move(contents), cur_path, GetFileModificationTime(cur_path));
  }

  // Try to find relative to the build directory given in the symbols.
  if (!file_build_dir.empty()) {
    if (!IsPathAbsolute(file_build_dir)) {
      // Relative build directory.
      //
      // Try to apply the prefs combined with the file build directory. As of this writing the
      // Fuchsia build produces relative build directories from the symbols. This normally maps back
      // to the same place as the preference but will be different when shelling out to the separate
      // Zircon build). Even when we fix the multiple build mess in Fuchsia, this relative directory
      // feature can be useful for projects building in different parts.
      for (const auto& cur : build_dir_prefs_) {
        std::string cur_path = CatPathComponents(cur, CatPathComponents(file_build_dir, file_name));
        if (files::ReadFileToString(cur_path, &contents))
          return FileData(std::move(contents), cur_path, GetFileModificationTime(cur_path));
      }
    }

    // Try to find relative to the file build dir. Even do this if the file build dir is relative to
    // search relative to the current working directory.
    std::string cur_path = CatPathComponents(file_build_dir, file_name);
    if (files::ReadFileToString(cur_path, &contents))
      return FileData(std::move(contents), cur_path, GetFileModificationTime(cur_path));
  }

  // Fall back on reading relative to the working directory.
  if (files::ReadFileToString(file_name, &contents))
    return FileData(std::move(contents), file_name, GetFileModificationTime(file_name));

  return Err("Source file not found: " + file_name);
}

}  // namespace zxdb
