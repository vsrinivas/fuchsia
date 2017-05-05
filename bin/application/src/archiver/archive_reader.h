// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_ARCHIVER_ARCHIVE_READER_H_
#define APPLICATION_SRC_ARCHIVER_ARCHIVE_READER_H_

#include <vector>

#include "application/src/archiver/format.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/strings/string_view.h"

namespace archive {

class ArchiveReader {
 public:
  explicit ArchiveReader(ftl::UniqueFD fd);
  ~ArchiveReader();
  ArchiveReader(const ArchiveReader& other) = delete;

  bool Read();

  template <typename Callback>
  void ListDirectory(Callback callback) {
    for (const auto& entry : directory_table_)
      callback(GetPathView(entry));
  }

  bool ExtractFile(ftl::StringView archive_path, const char* output_path);

  ftl::UniqueFD TakeFileDescriptor();

  ftl::StringView GetPathView(const DirectoryTableEntry& entry);

 private:
  bool ReadIndex();
  bool ReadDirectory();

  IndexEntry* GetIndexEntry(uint64_t type);

  ftl::UniqueFD fd_;
  std::vector<IndexEntry> index_;
  std::vector<DirectoryTableEntry> directory_table_;
  std::vector<char> path_data_;
};  // namespace archive

}  // namespace archive

#endif  // APPLICATION_SRC_ARCHIVER_ARCHIVE_READER_H_
