// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/file_util.h"

#include <filesystem>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

std::string_view ExtractLastFileComponent(std::string_view path) {
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos)
    return path;
  return path.substr(last_slash + 1);
}

bool IsPathAbsolute(const std::string& path) { return !path.empty() && path[0] == '/'; }

bool PathContainsFromRight(std::string_view path, std::string_view right_query) {
  return StringEndsWith(path, right_query) &&
         (path.size() == right_query.size() || path[path.size() - right_query.size() - 1] == '/');
}

std::string CatPathComponents(const std::string& first, const std::string& second) {
  // Second component shouldn't begin with a slash.
  FXL_DCHECK(second.empty() || second[0] != '/');

  std::string result;
  result.reserve(first.size() + second.size() + 1);
  result.append(first);

  if (!first.empty() && !second.empty() && first.back() != '/')
    result.push_back('/');
  result.append(second);

  return result;
}

std::time_t GetFileModificationTime(const std::string& path) {
  std::error_code ec;
  std::filesystem::file_time_type last_write = std::filesystem::last_write_time(path, ec);
  if (ec)
    return 0;

  return std::filesystem::file_time_type::clock::to_time_t(last_write);
}

}  // namespace zxdb
