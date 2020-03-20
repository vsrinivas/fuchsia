// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/allow_list.h"

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"

namespace component {

AllowList::AllowList(const fxl::UniqueFD& dir, const std::string& path) : allow_all_(false) {
  std::string result;
  if (!files::ReadFileToStringAt(dir.get(), path, &result)) {
    FXL_LOG(ERROR) << "Failed to read allowlist at " << path << ", will deny all usage attempts";
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
      internal_set_.insert(std::move(line));
    }
  }
}

}  // namespace component
