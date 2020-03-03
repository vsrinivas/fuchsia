// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/allow_list.h"

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"

namespace component {

AllowList::AllowList(const fxl::UniqueFD& dir, const std::string& path,
                     AllowList::Expectation expected) {
  std::string result;
  if (!files::ReadFileToStringAt(dir.get(), path, &result)) {
    if (expected == AllowList::kExpected) {
      FXL_LOG(ERROR) << "Failed to read allowlist " << path;
    }
    file_found_ = false;
    return;
  }

  auto lines = fxl::SplitStringCopy(result, "\n", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  for (auto& line : lines) {
    // Skip over comments.
    if (line.rfind("#", 0) != 0) {
      internal_set_.insert(std::move(line));
    }
  }
  file_found_ = true;
}

}  // namespace component
