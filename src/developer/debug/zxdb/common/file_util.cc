// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/file_util.h"

#include <lib/syslog/cpp/macros.h>

#include <chrono>
#include <filesystem>

#include "src/developer/debug/zxdb/common/string_util.h"

namespace zxdb {

std::string_view ExtractLastFileComponent(std::string_view path) {
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos)
    return path;
  return path.substr(last_slash + 1);
}

bool IsPathAbsolute(const std::string& path) { return !path.empty() && path[0] == '/'; }

bool PathEndsWith(std::string_view path, std::string_view right_query) {
  return StringEndsWith(path, right_query) &&
         (path.size() == right_query.size() || path[path.size() - right_query.size() - 1] == '/');
}

std::string CatPathComponents(const std::string& first, const std::string& second) {
  // Second component shouldn't begin with a slash.
  FX_DCHECK(second.empty() || second[0] != '/');

  std::string result;
  result.reserve(first.size() + second.size() + 1);
  result.append(first);

  if (!first.empty() && !second.empty() && first.back() != '/')
    result.push_back('/');
  result.append(second);

  return result;
}

std::string NormalizePath(const std::string& path) {
  return std::filesystem::path(path).lexically_normal();
}

std::time_t GetFileModificationTime(const std::string& path) {
  std::error_code ec;
  std::filesystem::file_time_type last_write = std::filesystem::last_write_time(path, ec);
  if (ec)
    return 0;

  return std::chrono::duration_cast<std::chrono::seconds>(last_write.time_since_epoch()).count();
}

bool PathStartsWith(const std::filesystem::path& path, const std::filesystem::path& base) {
  // Only absolute paths can be compared.
  if (!path.is_absolute() || !base.is_absolute())
    return false;
  auto path_it = path.begin();
  for (const auto& ancestor : base) {
    if (path_it == path.end())
      return false;
    if (ancestor != *path_it)
      return false;
    path_it++;
  }
  return true;
}

std::filesystem::path PathRelativeTo(const std::filesystem::path& path,
                                     const std::filesystem::path& base) {
  FX_CHECK(path.is_absolute() && base.is_absolute());
  auto base_it = base.begin();
  auto path_it = path.begin();
  while (base_it != base.end() && path_it != path.end() && *base_it == *path_it) {
    base_it++;
    path_it++;
  }
  std::filesystem::path res;
  while (base_it != base.end()) {
    res.append("..");
    base_it++;
  }
  while (path_it != path.end()) {
    res.append(path_it->string());
    path_it++;
  }
  return res;
}

}  // namespace zxdb
