// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/file_util.h"

namespace zxdb {

fxl::StringView ExtractLastFileComponent(fxl::StringView path) {
  size_t last_slash = path.rfind('/');
  if (last_slash == std::string::npos)
    return path;
  return path.substr(last_slash + 1);
}

bool IsPathAbsolute(const std::string& path) {
  return !path.empty() && path[0] == '/';
}

std::string CatPathComponents(const std::string& first,
                              const std::string& second) {
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

}  // namespace zxdb
