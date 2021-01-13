// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/pkg/lib/far/cpp/archive_reader.h"

#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>

#include <limits>
#include <utility>

#include <fbl/algorithm.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/concatenate.h"
#include "src/sys/pkg/lib/far/cpp/file_operations.h"
#include "src/sys/pkg/lib/far/cpp/format.h"

namespace archive {
namespace {

struct PathComparator {
  const ArchiveReader* reader = nullptr;

  bool operator()(const DirectoryTableEntry& lhs, const std::string_view& rhs) {
    return reader->GetPathView(lhs) < rhs;
  }
};

}  // namespace

ArchiveReader::ArchiveReader(fbl::unique_fd fd) : fd_(std::move(fd)) {}

ArchiveReader::~ArchiveReader() = default;

bool ArchiveReader::Read() {
  if (ReadIndex() && ReadDirectory()) {
    return ContentChunksOK();
  }
  return false;
}

bool ArchiveReader::Extract(std::string_view output_dir) const {
  for (const auto& entry : directory_table_) {
    std::string path = fxl::Concatenate({output_dir, "/", GetPathView(entry)});
    std::string dir = files::GetDirectoryName(path);
    if (!dir.empty() && !files::IsDirectory(dir) && !files::CreateDirectory(dir)) {
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

bool ArchiveReader::ExtractFile(std::string_view archive_path, const char* output_path) const {
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

bool ArchiveReader::CopyFile(std::string_view archive_path, int dst_fd) const {
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

bool ArchiveReader::GetDirectoryEntryByIndex(uint64_t index, DirectoryTableEntry* entry) const {
  if (index >= directory_table_.size())
    return false;
  *entry = directory_table_[index];
  return true;
}

bool ArchiveReader::GetDirectoryEntryByPath(std::string_view archive_path,
                                            DirectoryTableEntry* entry) const {
  uint64_t index = 0;
  return GetDirectoryIndexByPath(archive_path, &index) && GetDirectoryEntryByIndex(index, entry);
}

bool ArchiveReader::GetDirectoryIndexByPath(std::string_view archive_path, uint64_t* index) const {
  PathComparator comparator;
  comparator.reader = this;

  auto it =
      std::lower_bound(directory_table_.begin(), directory_table_.end(), archive_path, comparator);
  if (it == directory_table_.end() || GetPathView(*it) != archive_path)
    return false;
  *index = it - directory_table_.begin();
  return true;
}

fbl::unique_fd ArchiveReader::TakeFileDescriptor() { return std::move(fd_); }

std::string_view ArchiveReader::GetPathView(const DirectoryTableEntry& entry) const {
  return std::string_view(path_data_.data() + entry.name_offset, entry.name_length);
}

bool ArchiveReader::ReadIndex() {
  if (lseek(fd_.get(), 0, SEEK_SET) < 0) {
    fprintf(stderr, "error: Failed to seek to beginning of archive.\n");
    return false;
  }

  IndexChunk index_chunk;
  if (!ReadObject(fd_.get(), &index_chunk)) {
    fprintf(stderr, "error: Failed read index chunk. Is this file an archive?\n");
    return false;
  }

  if (index_chunk.magic != kMagic) {
    fprintf(stderr, "error: Index chunk missing magic. Is this file an archive?\n");
    return false;
  }

  if (index_chunk.length % sizeof(IndexEntry) != 0 ||
      index_chunk.length > std::numeric_limits<uint64_t>::max() - sizeof(IndexChunk)) {
    fprintf(stderr, "error: Invalid index chunk length.\n");
    return false;
  }

  index_.resize(index_chunk.length / sizeof(IndexEntry));
  if (!ReadVector(fd_.get(), &index_)) {
    fprintf(stderr, "error: Failed to read contents of index chunk.\n");
    return false;
  }

  uint64_t next_offset = sizeof(IndexChunk) + index_chunk.length;
  uint64_t prev_type = 0;
  for (const auto& entry : index_) {
    if (entry.offset != next_offset) {
      fprintf(stderr, "error: Chunk at offset %" PRIu64 " not tightly packed.\n", entry.offset);
      return false;
    }
    if (entry.length % 8 != 0) {
      fprintf(stderr, "error: Chunk length %" PRIu64 " not aligned to 8 byte boundary.\n",
              entry.length);
      return false;
    }
    if (entry.length > std::numeric_limits<uint64_t>::max() - entry.offset) {
      fprintf(stderr, "error: Chunk length %" PRIu64 " overflowed total archive size.\n",
              entry.length);
      return false;
    }
    if (prev_type == entry.type) {
      fprintf(stderr, "error: duplicate chunk of type 0x%" PRIx64 " in the index.\n", entry.type);
      return false;
    }
    if (prev_type > entry.type) {
      fprintf(stderr,
              "error: invalid index entry order, chunk type 0x%" PRIx64
              " before chunk type 0x%" PRIx64 ".\n",
              prev_type, entry.type);
      return false;
    }
    prev_type = entry.type;
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
    fprintf(stderr, "error: Invalid directory chunk length: %" PRIu64 ".\n", dir_entry->length);
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

  return DirEntriesOK();
}

bool ArchiveReader::DirEntriesOK() const {
  for (size_t i = 0; i < directory_table_.size(); i++) {
    const DirectoryTableEntry& cur = directory_table_[i];
    uint64_t cur_start = cur.name_offset;
    if (cur_start + cur.name_length > path_data_.size()) {
      fprintf(stderr, "error: invalid dir name length.\n");
      return false;
    }

    // Validate directory name.
    std::string_view cur_name = GetPathView(cur);
    if (!DirNameOK(cur_name)) {
      return false;
    }

    // Verify lexicographical order of dir name strings.
    if (i == 0)
      continue;
    const DirectoryTableEntry& prev = directory_table_[i - 1];
    uint64_t prev_start = prev.name_offset;
    std::string_view prev_name(reinterpret_cast<const char*>(&path_data_[prev_start]),
                               prev.name_length);
    if (prev_name >= cur_name) {
      fprintf(stderr, "invalid order of dir names.\n");
      return false;
    }
  }
  return true;
}

bool ArchiveReader::ContentChunksOK() const {
  uint64_t prev_end = index_.back().offset + index_.back().length;
  for (size_t i = 0; i < directory_table_.size(); i++) {
    const DirectoryTableEntry& cur = directory_table_[i];
    uint64_t cur_start = cur.data_offset;
    if (cur_start % kContentAlignment != 0) {
      fprintf(stderr, "content chunk at index %zu not aligned on a 4096 byte boundary.\n", i);
      return false;
    }

    // Verify packing and ordering versus the previous chunk.
    if (prev_end > cur_start) {
      fprintf(stderr, "content chunk at index %lu starts before the previous chunk ends.\n", i);
      return false;
    }
    uint64_t expected_offset = fbl::round_up(prev_end, kContentAlignment);
    if (cur_start != expected_offset) {
      fprintf(stderr,
              "content chunk violates the tightly packed constraint: expected offset: 0x%" PRIx64
              ", actual offset: 0x%" PRIx64 ".\n",
              expected_offset, cur_start);
      return false;
    }
    prev_end = cur_start + cur.data_length;
  }

  // Ensure the last content chunk does not extend beyond the end of the file.
  if (directory_table_.size() != 0) {
    const DirectoryTableEntry& last_entry = directory_table_.back();
    uint64_t expected_size =
        fbl::round_up(last_entry.data_offset + last_entry.data_length, kContentAlignment);
    struct stat sb;
    if (fstat(fd_.get(), &sb) == -1) {
      fprintf(stderr, "can't check archive size. fstat() on underlying file descriptor failed.\n");
      return false;
    }
    if (static_cast<uint64_t>(sb.st_size) != expected_size) {
      fprintf(stderr, "last content chunk extends beyond end of file.\n");
      return false;
    }
  }

  return true;
}

// ArchiveReader::DirNameOK checks the argument for compliance to the FAR archive spec.
bool ArchiveReader::DirNameOK(std::string_view name) const {
  if (name.size() == 0) {
    fprintf(stderr, "error: name has zero length.\n");
    return false;
  }
  if (name[0] == '/') {
    fprintf(stderr, "error: name must not start with '/'.\n");
    return false;
  }
  if (name.back() == '/') {
    fprintf(stderr, "error: name must not end with '/'.\n");
    return false;
  }

  enum ParserState {
    kEmpty = 0,
    kDot,
    kDotDot,
    kOther,
  };

  ParserState state = kEmpty;

  for (auto& c : name) {
    switch (c) {
      case '\0':
        fprintf(stderr, "error: name contains a null byte.\n");
        return false;
      case '/':
        switch (state) {
          case kEmpty:
            fprintf(stderr, "error: name contains empty segment.\n");
            return false;
          case kDot:
            fprintf(stderr, "error: name contains '.' segment.\n");
            return false;
          case kDotDot:
            fprintf(stderr, "error: name contains '..' segment.\n");
            return false;
          case kOther:
            state = kEmpty;
        }
        break;
      case '.':
        switch (state) {
          case kEmpty:
            state = kDot;
            break;
          case kDot:
            state = kDotDot;
            break;
          case kDotDot:
          case kOther:
            state = kOther;
        }
        break;
      default:
        state = kOther;
    }
  }

  switch (state) {
    case kDot:
      fprintf(stderr, "error: name contains '.' segment.\n");
      return false;
    case kDotDot:
      fprintf(stderr, "error: name contains '..' segment.\n");
      return false;
    default:
      return true;
  }
}

const IndexEntry* ArchiveReader::GetIndexEntry(uint64_t type) const {
  for (auto& entry : index_) {
    if (entry.type == type)
      return &entry;
  }
  return nullptr;
}

}  // namespace archive
