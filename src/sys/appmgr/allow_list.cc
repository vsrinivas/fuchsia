// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/allow_list.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"

namespace component {

AllowList::AllowList(const fbl::unique_fd& dir, const std::string& path) : allow_all_(false) {
  std::string result;
  if (!files::ReadFileToStringAt(dir.get(), path, &result)) {
    FX_LOGS(ERROR) << "Failed to read allowlist at " << path << ", will deny all usage attempts";
    return;
  }

  auto lines = fxl::SplitStringCopy(result, "\n", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  for (auto& line : lines) {
    // If a line with just a * is present, consider that to wildcard allow
    // everything.  This is designed so that we can require all allowlists
    // to always be present, and fail closed rather than open.
    if (line.length() == 1 && line[0] == '*') {
      allow_all_ = true;
      continue;
    }

    // Skip over comments.
    if (line.rfind("#", 0) != 0) {
      list_.push_back(std::move(line));
    }
  }
}

bool AllowList::IsAllowed(const FuchsiaPkgUrl& in_url) const {
  if (allow_all_) {
    return true;
  }
  for (const auto& item : list_) {
    if (IsMatch(item, in_url)) {
      return true;
    }
  }
  return false;
}

// static
bool AllowList::IsMatch(const std::string& allowlist_item, const FuchsiaPkgUrl& in_url) {
  FuchsiaPkgUrl allowlist_url;
  if (!allowlist_url.Parse(allowlist_item)) {
    // Not a valid fuchsia-pkg URL, skip.
    return false;
  }

  // Do not check variant or hash.
  return (allowlist_url.host_name() == in_url.host_name() &&
          allowlist_url.package_name() == in_url.package_name() &&
          allowlist_url.resource_path() == in_url.resource_path());
}

}  // namespace component
