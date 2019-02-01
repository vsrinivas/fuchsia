// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/far/far.h"

#include "garnet/lib/far/archive_reader.h"

struct far_reader {
  archive::ArchiveReader* impl;
};

bool far_reader_create(far_reader_t* reader) {
  far_reader_t result = new far_reader;
  result->impl = nullptr;
  *reader = result;
  return true;
}

bool far_reader_destroy(far_reader_t reader) {
  if (reader->impl)
    delete reader->impl;
  delete reader;
  return true;
}

bool far_reader_read_fd(far_reader_t reader, int fd) {
  reader->impl = new archive::ArchiveReader(fxl::UniqueFD(fd));
  return reader->impl->Read();
}

bool far_reader_get_count(far_reader_t reader, uint64_t* count) {
  *count = reader->impl->file_count();
  return true;
}

bool far_reader_get_index(far_reader_t reader, const char* path,
                          size_t path_length, uint64_t* index) {
  return reader->impl->GetDirectoryIndexByPath(
      fxl::StringView(path, path_length), index);
}

bool far_reader_get_path(far_reader_t reader, uint64_t index, const char** path,
                         size_t* path_length) {
  archive::DirectoryTableEntry entry;
  if (!reader->impl->GetDirectoryEntryByIndex(index, &entry))
    return false;
  fxl::StringView path_view = reader->impl->GetPathView(entry);
  *path = path_view.data();
  *path_length = path_view.size();
  return true;
}

bool far_reader_get_content(far_reader_t reader, uint64_t index,
                            uint64_t* offset, uint64_t* length) {
  archive::DirectoryTableEntry entry;
  if (!reader->impl->GetDirectoryEntryByIndex(index, &entry))
    return false;
  *offset = entry.data_offset;
  *length = entry.data_length;
  return true;
}
