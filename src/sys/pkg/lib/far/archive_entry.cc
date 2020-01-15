// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/pkg/lib/far/archive_entry.h"

#include <utility>

namespace archive {

ArchiveEntry::ArchiveEntry() = default;

ArchiveEntry::ArchiveEntry(std::string src_path, std::string dst_path)
    : src_path(std::move(src_path)), dst_path(std::move(dst_path)) {}

ArchiveEntry::~ArchiveEntry() = default;

ArchiveEntry::ArchiveEntry(ArchiveEntry&& other)
    : src_path(std::move(other.src_path)), dst_path(std::move(other.dst_path)) {}

ArchiveEntry& ArchiveEntry::operator=(ArchiveEntry&& other) {
  swap(other);
  return *this;
}

void ArchiveEntry::swap(ArchiveEntry& other) {
  src_path.swap(other.src_path);
  dst_path.swap(other.dst_path);
}

}  // namespace archive
