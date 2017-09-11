// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/far/archive_writer.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "garnet/lib/far/alignment.h"
#include "garnet/lib/far/file_operations.h"
#include "garnet/lib/far/format.h"
#include "lib/fxl/files/file_descriptor.h"
#include "lib/fxl/files/unique_fd.h"

namespace archive {

ArchiveWriter::ArchiveWriter() = default;

ArchiveWriter::~ArchiveWriter() = default;

bool ArchiveWriter::Add(ArchiveEntry entry) {
  size_t size = entry.dst_path.size();
  if (size > std::numeric_limits<uint16_t>::max())
    return false;
  if (size > std::numeric_limits<uint32_t>::max() - total_path_length_)
    return false;
  // TODO(abarth): Add more entry.dst_path validation.
  dirty_ = true;
  entries_.push_back(std::move(entry));
  total_path_length_ += size;
  return true;
}

bool ArchiveWriter::Write(int fd) {
  if (dirty_) {
    std::sort(entries_.begin(), entries_.end());
    dirty_ = false;
  }

  if (HasDuplicateEntries())
    return false;

  if (lseek(fd, 0, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to beginning of archive.\n");
    return false;
  }

  uint64_t index_count = entries_.empty() ? 0 : 2;
  uint64_t next_chunk = 0;

  IndexChunk index;
  index.length = index_count * sizeof(IndexEntry);
  next_chunk += sizeof(IndexChunk) + index.length;
  if (!WriteObject(fd, index)) {
    fprintf(stderr, "error: Failed to write index chunk.\n");
    return false;
  }

  if (entries_.empty())
    return true;  // No files to store in the archive.

  IndexEntry dir_entry;
  dir_entry.type = kDirType;
  dir_entry.offset = next_chunk;
  dir_entry.length = entries_.size() * sizeof(DirectoryTableEntry);
  next_chunk += dir_entry.length;
  if (!WriteObject(fd, dir_entry)) {
    fprintf(stderr, "error: Failed to write directory index chunk.\n");
    return false;
  }

  IndexEntry dirnames_entry;
  dirnames_entry.type = kDirnamesType;
  dirnames_entry.offset = next_chunk;
  dirnames_entry.length = AlignTo8ByteBoundary(total_path_length_);
  next_chunk += dirnames_entry.length;
  if (!WriteObject(fd, dirnames_entry)) {
    fprintf(stderr, "error: Failed to write directory names index chunk\n");
    return false;
  }

  uint32_t name_offset = 0;
  uint64_t data_offset = AlignToPage(next_chunk);
  std::vector<DirectoryTableEntry> directory_table(entries_.size());
  for (size_t i = 0; i < entries_.size(); ++i) {
    const ArchiveEntry& entry = entries_[i];
    DirectoryTableEntry& directory_entry = directory_table[i];

    struct stat info;
    if (stat(entry.src_path.c_str(), &info) != 0) {
      fprintf(stderr, "error: Failed to read length of file: %s\n",
              entry.src_path.c_str());
      return false;
    }
    uint64_t data_length = info.st_size;

    if (data_length > std::numeric_limits<uint64_t>::max() - data_offset) {
      fprintf(stderr, "error: File overflowed total archive size: %s\n",
              entry.src_path.c_str());
      return false;
    }

    directory_entry.name_offset = name_offset;
    directory_entry.name_length = entry.dst_path.size();
    directory_entry.data_offset = data_offset;
    directory_entry.data_length = data_length;

    name_offset += directory_entry.name_length;
    data_offset = AlignToPage(data_offset + data_length);
  }

  if (!WriteVector(fd, directory_table)) {
    fprintf(stderr, "error: Failed to write directory table.\n");
    return false;
  }

  std::vector<char> path_data(total_path_length_);
  char* pos = path_data.data();
  for (const auto& entry : entries_) {
    memcpy(pos, entry.dst_path.data(), entry.dst_path.size());
    pos += entry.dst_path.size();
  }

  if (!WriteVector(fd, path_data)) {
    fprintf(stderr, "error: Failed to write path data.\n");
    return false;
  }

  for (size_t i = 0; i < entries_.size(); ++i) {
    const ArchiveEntry& entry = entries_[i];
    const DirectoryTableEntry& directory_entry = directory_table[i];

    if (lseek(fd, directory_entry.data_offset, SEEK_SET) < 0) {
      fprintf(stderr, "error: Failed to seek to data offset.\n");
      return false;
    }
    if (!CopyPathToFile(entry.src_path.c_str(), fd,
                        directory_entry.data_length)) {
      fprintf(stderr, "error: Failed to write file data: %s\n",
              entry.src_path.c_str());
      return false;
    }
  }

  if (!entries_.empty()) {
    const DirectoryTableEntry& directory_entry = directory_table.back();
    uint64_t end = directory_entry.data_offset + directory_entry.data_length;
    if (ftruncate(fd, AlignToPage(end)) < 0) {
      fprintf(stderr, "error: Failed to truncate archive to proper length.\n");
      return false;
    }
  }

  return true;
}

bool ArchiveWriter::HasDuplicateEntries() {
  for (size_t i = 0; i + 1 < entries_.size(); ++i) {
    if (entries_[i].dst_path == entries_[i + 1].dst_path) {
      fprintf(stderr, "error: Archive has duplicate path: '%s'\n",
              entries_[i].dst_path.c_str());
      return true;
    }
  }
  return false;
}

}  // namespace archive
