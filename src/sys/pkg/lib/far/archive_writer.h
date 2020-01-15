// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_ARCHIVE_WRITER_H_
#define GARNET_LIB_FAR_ARCHIVE_WRITER_H_

#include <stdint.h>

#include <vector>

#include "src/sys/pkg/lib/far/archive_entry.h"

namespace archive {

class ArchiveWriter {
 public:
  ArchiveWriter();
  ~ArchiveWriter();
  ArchiveWriter(const ArchiveWriter& other) = delete;

  bool Add(ArchiveEntry entry);
  bool Write(int fd);

 private:
  bool HasDuplicateEntries();

  std::vector<ArchiveEntry> entries_;
  bool dirty_ = true;
  uint64_t total_path_length_ = 0;
};

}  // namespace archive

#endif  // GARNET_LIB_FAR_ARCHIVE_WRITER_H_
