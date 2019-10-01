// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/appmgr/allowlist.h"

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"

namespace component {

Allowlist::Allowlist(const std::string& path) {
  std::string result;
  if (!files::ReadFileToString(path, &result)) {
    FXL_LOG(ERROR) << "Failed to read allowlist " << path;
    return;
  }

  auto lines = fxl::SplitStringCopy(result, "\n", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  for (auto& line : lines) {
    internal_set_.insert(std::move(line));
  }
}

}  // namespace component
