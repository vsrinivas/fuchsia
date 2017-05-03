// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/src/archiver/archive_writer.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <limits>
#include <vector>

#include "application/src/archiver/format.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/unique_fd.h"

namespace archive {
namespace {

const char* GetBytes(const void* data) {
  return static_cast<const char*>(data);
}

uint64_t AlignToPage(uint64_t offset) {
  return (offset + 4095u) & ~4095ull;
}

size_t GetBufferLengthForString(const std::string& string) {
  return string.size() + (string.size() % 2);
}

template<typename T>
bool WriteObject(int fd, const T& object) {
  return ftl::WriteFileDescriptor(fd, GetBytes(&object), sizeof(T));
}

template<typename T>
bool WriteVector(int fd, const std::vector<T>& vector) {
  size_t requested = vector.size() * sizeof(T);
  return ftl::WriteFileDescriptor(fd, GetBytes(vector.data()), requested);
}

bool CopyFile(int dst_fd, const char* src_path, uint64_t data_length) {
  char buffer[64 * 1024];
  ftl::UniqueFD src_fd(open(src_path, O_RDONLY));
  if (src_fd.get() < 0)
    return false;
  uint64_t copied = 0;
  while (true) {
    ssize_t actual = read(src_fd.get(), buffer, sizeof(buffer));
    if (actual < 0)
      return false;
    if (actual == 0)
      return copied == data_length;
    if (data_length - copied < static_cast<size_t>(actual))
      return false;
    if (!ftl::WriteFileDescriptor(dst_fd, buffer, actual))
      return false;
  }
}

bool PadToEndOfPage(int fd, uint64_t length) {
  uint64_t next_chunk = AlignToPage(length);
  uint64_t pad_count = next_chunk - length;
  if (pad_count) {
    char buffer[pad_count];
    memset(buffer, 0, pad_count);
    ssize_t actual = write(fd, buffer, pad_count);
    return actual == static_cast<ssize_t>(pad_count);
  }
  return true;
}

} // namespace

ArchiveWriter::ArchiveWriter() = default;

ArchiveWriter::~ArchiveWriter() = default;

bool ArchiveWriter::Add(ArchiveEntry entry) {
  if (entry.dst_path.size() > std::numeric_limits<uint16_t>::max())
    return false;
  // TODO(abarth): Add more entry.dst_path validation.
  dirty_ = true;
  entries_.push_back(std::move(entry));
  return true;
}

bool ArchiveWriter::Write(int fd) {
  if (dirty_) {
    std::sort(entries_.begin(), entries_.end());
    dirty_ = false;
  }

  if (HasDuplicateEntries())
    return false;

  if (lseek(fd, 0, SEEK_CUR) < 0)
    return false;

  uint64_t index_count = entries_.empty() ? 0 : 2;
  uint64_t next_chunk = 0;

  IndexChunk index;
  index.length = index_count * sizeof(IndexEntry);
  next_chunk += sizeof(IndexChunk) + index.length;
  if (!WriteObject(fd, index))
    return false;

  if (entries_.empty())
    return true; // No files to store in the archive.

  IndexEntry dir_entry(kDirType);
  dir_entry.offset = next_chunk;
  dir_entry.length = entries_.size() * sizeof(DirectoryTableEntry);
  next_chunk += dir_entry.length;
  if (!WriteObject(fd, dir_entry))
    return false;

  IndexEntry dirnames_entry(kDirnamesType);
  dirnames_entry.offset = next_chunk;
  if (!GetDirnamesLength(&dirnames_entry.length))
    return false;
  next_chunk += dirnames_entry.length;
  if (dirnames_entry.length > std::numeric_limits<uint32_t>::max())
    return false; // Unreasonably large path names.
  if (!WriteObject(fd, dirnames_entry))
    return false;

  uint32_t name_offset = 0;
  uint64_t data_offset = AlignToPage(next_chunk);
  std::vector<DirectoryTableEntry> directory_table(entries_.size());
  for (size_t i = 0; i < entries_.size(); ++i) {
    const ArchiveEntry& entry = entries_[i];
    DirectoryTableEntry& directory_entry = directory_table[i];

    struct stat info;
    if (stat(entry.src_path.c_str(), &info) != 0)
      return false;
    uint64_t data_length = info.st_size;

    if (data_length > std::numeric_limits<uint64_t>::max() - data_offset)
      return false; // Overflowed total archive size.

    directory_entry.name_offset = name_offset;
    directory_entry.data_offset = data_offset;
    directory_entry.data_length = data_length;

    name_offset += sizeof(PathData) + entry.dst_path.size();
    data_offset = AlignToPage(data_offset + data_length);

    if (data_offset < directory_entry.data_offset)
      return false; // Overflowed total archive size.
  }

  if (!WriteVector(fd, directory_table))
    return false;

  for (const auto& entry : entries_) {
    PathData data;
    data.length = entry.dst_path.size();
    if (!WriteObject(fd, data))
      return false;
    if (!ftl::WriteFileDescriptor(fd, entry.dst_path.c_str(),
                                  GetBufferLengthForString(entry.dst_path)))
      return false;
  }

  if (!directory_table.empty()) {
    if (lseek(fd, directory_table[0].data_offset, SEEK_CUR) < 0)
       return false;
  }

  for (size_t i = 0; i < entries_.size(); ++i) {
    const ArchiveEntry& entry = entries_[i];
    DirectoryTableEntry& directory_entry = directory_table[i];

    if (!CopyFile(fd, entry.src_path.c_str(), directory_entry.data_length))
      return false;

    if (!PadToEndOfPage(fd, directory_entry.data_length))
      return false;
  }

  return true;
}

bool ArchiveWriter::GetDirnamesLength(uint64_t* result) const {
  uint64_t sum = entries_.size() * sizeof(PathData);
  for (const auto& entry : entries_) {
    size_t size = GetBufferLengthForString(entry.dst_path);
    if (size > std::numeric_limits<uint64_t>::max() - sum)
      return false; // Overflowed chunk size.
    sum += size;
  }
  *result = sum;
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

} // archive
