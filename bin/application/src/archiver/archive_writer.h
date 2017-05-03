// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_ARCHIVER_ARCHIVE_WRITER_H_
#define APPLICATION_SRC_ARCHIVER_ARCHIVE_WRITER_H_

#include <stdint.h>

#include <vector>

#include "application/src/archiver/archive_entry.h"

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
  bool GetDirnamesLength(uint64_t* result) const;

  std::vector<ArchiveEntry> entries_;
  bool dirty_ = true;
};

} // archive

#endif // APPLICATION_SRC_ARCHIVER_ARCHIVE_WRITER_H_
