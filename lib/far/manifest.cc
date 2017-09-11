// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/far/manifest.h"

#include <stdio.h>

#include "garnet/lib/far/archive_entry.h"
#include "garnet/lib/far/archive_writer.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/split_string.h"

namespace archive {

bool ReadManifest(fxl::StringView path, ArchiveWriter* writer) {
  std::string manifest;
  if (!files::ReadFileToString(path.ToString(), &manifest)) {
    fprintf(stderr, "error: Faile to read '%s'\n", path.ToString().c_str());
    return false;
  }

  std::vector<fxl::StringView> lines =
      fxl::SplitString(manifest, "\n", fxl::WhiteSpaceHandling::kKeepWhitespace,
                       fxl::SplitResult::kSplitWantNonEmpty);

  for (const auto& line : lines) {
    size_t offset = line.find('=');
    if (offset == std::string::npos)
      continue;
    writer->Add(ArchiveEntry(line.substr(offset + 1).ToString(),
                             line.substr(0, offset).ToString()));
  }

  return true;
}

}  // namespace archive
