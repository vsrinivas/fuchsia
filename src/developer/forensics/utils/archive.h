// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_ARCHIVE_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_ARCHIVE_H_

#include <fuchsia/mem/cpp/fidl.h>

#include <map>
#include <string>

namespace forensics {

struct ArchiveFileStats {
  size_t raw_bytes;
  size_t compressed_bytes;
};

// Bundles a map of filenames to string content into a single ZIP archive with DEFLATE compression.
// Also returns a map of the same filenames to size stats.
bool Archive(const std::map<std::string, std::string>& files, fuchsia::mem::Buffer* archive,
             std::map<std::string, ArchiveFileStats>* file_to_size_stats = nullptr);

// Unpack a ZIP archive into a map of filenames to string content.
bool Unpack(const fuchsia::mem::Buffer& archive, std::map<std::string, std::string>* files);

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_ARCHIVE_H_
