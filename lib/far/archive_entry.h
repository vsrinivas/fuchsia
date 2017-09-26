// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_ARCHIVE_ENTRY_H_
#define GARNET_LIB_FAR_ARCHIVE_ENTRY_H_

#include <string>

namespace archive {

struct ArchiveEntry {
  ArchiveEntry();
  ~ArchiveEntry();

  ArchiveEntry(std::string src_path, std::string dst_path);
  ArchiveEntry(const ArchiveEntry& other) = delete;
  ArchiveEntry(ArchiveEntry&& other);

  ArchiveEntry& operator=(const ArchiveEntry& other) = delete;
  ArchiveEntry& operator=(ArchiveEntry&& other);

  void swap(ArchiveEntry& other);

  std::string src_path;
  std::string dst_path;
};

// Comparies archive entries by dst_path;
inline bool operator<(const ArchiveEntry& lhs, const ArchiveEntry& rhs) {
  return lhs.dst_path < rhs.dst_path;
}

}  // namespace archive

#endif  // GARNET_LIB_FAR_ARCHIVE_ENTRY_H_
