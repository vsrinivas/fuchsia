// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"

#include <lib/syslog/cpp/macros.h>

#include <limits>

#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/lib/files/file.h"

namespace zxdb {

SourceFileProviderImpl::SourceFileProviderImpl(const std::vector<std::string>& source_map) {
  for (const auto& entry : source_map) {
    size_t pos = entry.find('=');
    if (pos == std::string::npos || pos == 0 || pos == entry.size() - 1)
      continue;  // Invalid entry.
    source_map_.emplace_back(entry.substr(0, pos), entry.substr(pos + 1));
  }
}

SourceFileProviderImpl::SourceFileProviderImpl(const SettingStore& settings)
    : SourceFileProviderImpl(settings.GetList(ClientSettings::Target::kSourceMap)) {}

ErrOr<SourceFileProvider::FileData> SourceFileProviderImpl::GetFileData(
    const std::string& file_name, const std::string& file_build_dir) const {
  std::string full_path = std::filesystem::path(file_build_dir) / file_name;
  std::string contents;

  // Try to apply source_map_ first so it could override.
  for (const auto& [base, replaced] : source_map_) {
    if (PathStartsWith(full_path, base)) {
      std::string replaced_path = replaced / PathRelativeTo(full_path, base);
      if (files::ReadFileToString(replaced_path, &contents))
        return FileData(std::move(contents), replaced_path, GetFileModificationTime(replaced_path));
    }
  }

  if (files::ReadFileToString(full_path, &contents))
    return FileData(std::move(contents), full_path, GetFileModificationTime(full_path));

  return Err(
      "Source file %s not found. You might want to adjust the source file remap setting. "
      "See \"get source-map\".",
      full_path.c_str());
}

}  // namespace zxdb
