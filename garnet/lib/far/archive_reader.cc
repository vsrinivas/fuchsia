// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/far/archive_reader.h"

#include <inttypes.h>
#include <unistd.h>

#include <limits>
#include <utility>

#include "garnet/lib/far/file_operations.h"
#include "garnet/lib/far/format.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/files/path.h"
#include "lib/fxl/strings/concatenate.h"

namespace archive {
namespace {

struct PathComparator {
  const ArchiveReader* reader = nullptr;

  bool operator()(const DirectoryTableEntry& lhs, const fxl::StringView& rhs) {
    return reader->GetPathView(lhs) < rhs;
  }
};

}  // namespace

ArchiveReader::ArchiveReader(fxl::UniqueFD fd) : fd_(std::move(fd)) {}

ArchiveReader::~ArchiveReader() = default;

bool ArchiveReader::Read() { return ReadIndex() && ReadDirectory(); }

bool ArchiveReader::Extract(fxl::StringView output_dir) const {
  for (const auto& entry : directory_table_) {
    std::string path = fxl::Concatenate({output_dir, "/", GetPathView(entry)});
    std::string dir = files::GetDirectoryName(path);
    if (!dir.empty() && !files::IsDirectory(dir) &&
        !files::CreateDirectory(dir)) {
      fprintf(stderr, "error: Failed to create directory '%s'.\n", dir.c_str());
      return false;
    }
    if (lseek(fd_.get(), entry.data_offset, SEEK_SET) < 0) {
      fprintf(stderr, "error: Failed to seek to offset of file.\n");
      return false;
    }
    if (!CopyFileToPath(fd_.get(), path.c_str(), entry.data_length)) {
      fprintf(stderr, "error: Failed write contents to '%s'.\n", path.c_str());
      return false;
    }
  }
  return true;
}

bool ArchiveReader::ExtractFile(fxl::StringView archive_path,
                                const char* output_path) const {
  DirectoryTableEntry entry;
  if (!GetDirectoryEntryByPath(archive_path, &entry))
    return false;
  if (lseek(fd_.get(), entry.data_offset, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to offset of file.\n");
    return false;
  }
  if (!CopyFileToPath(fd_.get(), output_path, entry.data_length)) {
    fprintf(stderr, "error: Failed write contents to '%s'.\n", output_path);
    return false;
  }
  return true;
}

bool ArchiveReader::CopyFile(fxl::StringView archive_path, int dst_fd) const {
  DirectoryTableEntry entry;
  if (!GetDirectoryEntryByPath(archive_path, &entry))
    return false;
  if (lseek(fd_.get(), entry.data_offset, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to offset of file.\n");
    return false;
  }
  if (!CopyFileToFile(fd_.get(), dst_fd, entry.data_length)) {
    fprintf(stderr, "error: Failed write contents.\n");
    return false;
  }
  return true;
}

bool ArchiveReader::GetDirectoryEntryByIndex(uint64_t index,
                                             DirectoryTableEntry* entry) const {
  if (index >= directory_table_.size())
    return false;
  *entry = directory_table_[index];
  return true;
}

bool ArchiveReader::GetDirectoryEntryByPath(fxl::StringView archive_path,
                                            DirectoryTableEntry* entry) const {
  uint64_t index = 0;
  return GetDirectoryIndexByPath(archive_path, &index) &&
         GetDirectoryEntryByIndex(index, entry);
}

bool ArchiveReader::GetDirectoryIndexByPath(fxl::StringView archive_path,
                                            uint64_t* index) const {
  PathComparator comparator;
  comparator.reader = this;

  auto it = std::lower_bound(directory_table_.begin(), directory_table_.end(),
                             archive_path, comparator);
  if (it == directory_table_.end() || GetPathView(*it) != archive_path)
    return false;
  *index = it - directory_table_.begin();
  return true;
}

fxl::UniqueFD ArchiveReader::TakeFileDescriptor() { return std::move(fd_); }

fxl::StringView ArchiveReader::GetPathView(
    const DirectoryTableEntry& entry) const {
  return fxl::StringView(path_data_.data() + entry.name_offset,
                         entry.name_length);
}

bool ArchiveReader::ReadIndex() {
  if (lseek(fd_.get(), 0, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to beginning of archive.\n");
    return false;
  }

  IndexChunk index_chunk;
  if (!ReadObject(fd_.get(), &index_chunk)) {
    fprintf(stderr,
            "error: Failed read index chunk. Is this file an archive?\n");
    return false;
  }

  if (index_chunk.magic != kMagic) {
    fprintf(stderr,
            "error: Index chunk missing magic. Is this file an archive?\n");
    return false;
  }

  if (index_chunk.length % sizeof(IndexEntry) != 0 ||
      index_chunk.length >
          std::numeric_limits<uint64_t>::max() - sizeof(IndexChunk)) {
    fprintf(stderr, "error: Invalid index chunk length.\n");
    return false;
  }

  index_.resize(index_chunk.length / sizeof(IndexEntry));
  if (!ReadVector(fd_.get(), &index_)) {
    fprintf(stderr, "error: Failed to read contents of index chunk.\n");
    return false;
  }

  uint64_t next_offset = sizeof(IndexChunk) + index_chunk.length;
  for (const auto& entry : index_) {
    if (entry.offset != next_offset) {
      fprintf(stderr,
              "error: Chunk at offset %" PRIu64 " not tightly packed.\n",
              entry.offset);
      return false;
    }
    if (entry.length % 8 != 0) {
      fprintf(stderr,
              "error: Chunk length %" PRIu64
              " not aligned to 8 byte boundary.\n",
              entry.length);
      return false;
    }
    if (entry.length > std::numeric_limits<uint64_t>::max() - entry.offset) {
      fprintf(stderr,
              "error: Chunk length %" PRIu64
              " overflowed total archive size.\n",
              entry.length);
      return false;
    }
    next_offset = entry.offset + entry.length;
  }

  return true;
}

bool ArchiveReader::ReadDirectory() {
  const IndexEntry* dir_entry = GetIndexEntry(kDirType);
  if (!dir_entry) {
    fprintf(stderr, "error: Cannot find directory chunk.\n");
    return false;
  }
  if (dir_entry->length % sizeof(DirectoryTableEntry) != 0) {
    fprintf(stderr, "error: Invalid directory chunk length: %" PRIu64 ".\n",
            dir_entry->length);
    return false;
  }
  uint64_t file_count = dir_entry->length / sizeof(DirectoryTableEntry);
  directory_table_.resize(file_count);

  if (lseek(fd_.get(), dir_entry->offset, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to directory chunk.\n");
    return false;
  }
  if (!ReadVector(fd_.get(), &directory_table_)) {
    fprintf(stderr, "error: Failed to read directory table.\n");
    return false;
  }

  const IndexEntry* dirnames_entry = GetIndexEntry(kDirnamesType);
  if (!dirnames_entry) {
    fprintf(stderr, "error: Cannot find directory names chunk.\n");
    return false;
  }
  path_data_.resize(dirnames_entry->length);

  if (lseek(fd_.get(), dirnames_entry->offset, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to directory names chunk.\n");
    return false;
  }
  if (!ReadVector(fd_.get(), &path_data_)) {
    fprintf(stderr, "error: Failed to read directory names.\n");
    return false;
  }

  return true;
}

const IndexEntry* ArchiveReader::GetIndexEntry(uint64_t type) const {
  for (auto& entry : index_) {
    if (entry.type == type)
      return &entry;
  }
  return nullptr;
}

}  // namespace archive
