// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_FAR_ARCHIVE_READER_H_
#define GARNET_LIB_FAR_ARCHIVE_READER_H_

#include <vector>

#include "garnet/lib/far/format.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/strings/string_view.h"

namespace archive {

class ArchiveReader {
 public:
  explicit ArchiveReader(fxl::UniqueFD fd);
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

  bool Extract(fxl::StringView output_dir) const;
  bool ExtractFile(fxl::StringView archive_path, const char* output_path) const;
  bool CopyFile(fxl::StringView archive_path, int dst_fd) const;

  bool GetDirectoryEntryByIndex(uint64_t index,
                                DirectoryTableEntry* entry) const;
  bool GetDirectoryEntryByPath(fxl::StringView archive_path,
                               DirectoryTableEntry* entry) const;

  bool GetDirectoryIndexByPath(fxl::StringView archive_path,
                               uint64_t* index) const;

  fxl::UniqueFD TakeFileDescriptor();

  fxl::StringView GetPathView(const DirectoryTableEntry& entry) const;

 private:
  bool ReadIndex();
  bool ReadDirectory();

  const IndexEntry* GetIndexEntry(uint64_t type) const;

  fxl::UniqueFD fd_;
  std::vector<IndexEntry> index_;
  std::vector<DirectoryTableEntry> directory_table_;
  std::vector<char> path_data_;
};

}  // namespace archive

#endif  // GARNET_LIB_FAR_ARCHIVE_READER_H_
