// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include "application/src/archiver/archive_writer.h"
#include "application/src/archiver/manifest.h"
#include "lib/ftl/files/unique_fd.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "error: Insufficient arguments.\n"
        "Usuage: archiver <manifest> <archive>\n");
    return -1;
  }
  archive::ArchiveWriter writer;
  char* manifest = argv[1];
  if (!archive::ReadManifest(manifest, &writer))
    return -1;
  char* dst = argv[2];
  ftl::UniqueFD fd(open(dst, O_WRONLY | O_CREAT | O_TRUNC,
      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
  if (fd.get() < 0)
    return -1;
  return writer.Write(fd.get()) ? 0 : -1;
}
