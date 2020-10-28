// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_PKG_LIB_FAR_ARCHIVE_READER_H_
#define SRC_SYS_PKG_LIB_FAR_ARCHIVE_READER_H_

#include <string_view>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/sys/pkg/lib/far/format.h"

namespace archive {

class ArchiveReader {
 public:
  explicit ArchiveReader(fbl::unique_fd fd);
  ~ArchiveReader();
  ArchiveReader(const ArchiveReader& other) = delete;

  bool Read();

  uint64_t file_count() const { return directory_table_.size(); }

  template <typename Callback>
  void ListPaths(Callback callback) const {
    for (const auto& entry : directory_table_)
      callback(GetPathView(entry));
  }

  template <typename Callback>
  void ListDirectory(Callback callback) const {
    for (const auto& entry : directory_table_)
      callback(entry);
  }

  bool Extract(std::string_view output_dir) const;
  bool ExtractFile(std::string_view archive_path, const char* output_path) const;
  bool CopyFile(std::string_view archive_path, int dst_fd) const;

  bool GetDirectoryEntryByIndex(uint64_t index, DirectoryTableEntry* entry) const;
  bool GetDirectoryEntryByPath(std::string_view archive_path, DirectoryTableEntry* entry) const;

  bool GetDirectoryIndexByPath(std::string_view archive_path, uint64_t* index) const;

  fbl::unique_fd TakeFileDescriptor();

  std::string_view GetPathView(const DirectoryTableEntry& entry) const;

 private:
  bool ReadIndex();
  bool ReadDirectory();

  const IndexEntry* GetIndexEntry(uint64_t type) const;

  fbl::unique_fd fd_;
  std::vector<IndexEntry> index_;
  std::vector<DirectoryTableEntry> directory_table_;
  std::vector<char> path_data_;
};

}  // namespace archive

#endif  // SRC_SYS_PKG_LIB_FAR_ARCHIVE_READER_H_
