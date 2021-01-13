// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/pkg/lib/far/cpp/manifest.h"

#include <stdio.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/sys/pkg/lib/far/cpp/archive_entry.h"
#include "src/sys/pkg/lib/far/cpp/archive_writer.h"

namespace archive {

bool ReadManifest(std::string_view path, ArchiveWriter* writer) {
  std::string manifest;
  if (!files::ReadFileToString(std::string(path), &manifest)) {
    fprintf(stderr, "error: Fail to read '%s'\n", std::string(path).c_str());
    return false;
  }

  std::vector<std::string_view> lines =
      fxl::SplitString(manifest, "\n", fxl::WhiteSpaceHandling::kKeepWhitespace,
                       fxl::SplitResult::kSplitWantNonEmpty);

  for (const auto& line : lines) {
    size_t offset = line.find('=');
    if (offset == std::string::npos)
      continue;
    writer->Add(
        ArchiveEntry(std::string(line.substr(offset + 1)), std::string(line.substr(0, offset))));
  }

  return true;
}

}  // namespace archive
