// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/far/manifest.h"

#include <stdio.h>

#include "application/lib/far/archive_entry.h"
#include "application/lib/far/archive_writer.h"
#include "lib/ftl/files/file.h"
#include "lib/ftl/strings/split_string.h"

namespace archive {

bool ReadManifest(ftl::StringView path, ArchiveWriter* writer) {
  std::string manifest;
  if (!files::ReadFileToString(path.ToString(), &manifest)) {
    fprintf(stderr, "error: Faile to read '%s'\n", path.ToString().c_str());
    return false;
  }

  std::vector<ftl::StringView> lines =
      ftl::SplitString(manifest, "\n", ftl::WhiteSpaceHandling::kKeepWhitespace,
                       ftl::SplitResult::kSplitWantNonEmpty);

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
