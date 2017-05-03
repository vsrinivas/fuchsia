// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/src/archiver/manifest.h"

#include <stdio.h>

#include <fstream>

#include "application/src/archiver/archive_entry.h"
#include "application/src/archiver/archive_writer.h"

namespace archive {

bool ReadManifest(char* path, ArchiveWriter* writer) {
  std::ifstream in;
  in.open(path);
  if (!in.good()) {
    fprintf(stderr, "error: failed to open '%s'\n", path);
    return false;
  }

  for (std::string line; std::getline(in, line); ) {
    size_t offset = line.find('=');
    if (offset == std::string::npos)
      continue;
    writer->Add(ArchiveEntry(line.substr(offset + 1), line.substr(0, offset)));
  }

  in.close();
  return true;
}

} // archive
