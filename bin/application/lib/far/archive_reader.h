// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_LIB_FAR_ARCHIVE_READER_H_
#define APPLICATION_LIB_FAR_ARCHIVE_READER_H_

#include <vector>

#include "application/lib/far/format.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/strings/string_view.h"

namespace archive {

class ArchiveReader {
 public:
  explicit ArchiveReader(ftl::UniqueFD fd);
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

  bool Extract(ftl::StringView output_dir) const;
  bool ExtractFile(ftl::StringView archive_path, const char* output_path) const;
  bool CopyFile(ftl::StringView archive_path, int dst_fd) const;

  bool GetDirectoryEntryByIndex(uint64_t index,
                                DirectoryTableEntry* entry) const;
  bool GetDirectoryEntryByPath(ftl::StringView archive_path,
                               DirectoryTableEntry* entry) const;

  bool GetDirectoryIndexByPath(ftl::StringView archive_path,
                               uint64_t* index) const;

  ftl::UniqueFD TakeFileDescriptor();

  ftl::StringView GetPathView(const DirectoryTableEntry& entry) const;

 private:
  bool ReadIndex();
  bool ReadDirectory();

  const IndexEntry* GetIndexEntry(uint64_t type) const;

  ftl::UniqueFD fd_;
  std::vector<IndexEntry> index_;
  std::vector<DirectoryTableEntry> directory_table_;
  std::vector<char> path_data_;
};

}  // namespace archive

#endif  // APPLICATION_LIB_FAR_ARCHIVE_READER_H_
