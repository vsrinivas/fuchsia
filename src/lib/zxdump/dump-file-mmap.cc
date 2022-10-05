// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dump-file-mmap.h"

#include <sys/mman.h>

namespace zxdump::internal {

// The returned view is valid for the life of the Mmap.
fit::result<Error, ByteView> DumpFile::Mmap::ReadPermanent(FileRange where) {
  auto result = ReadEphemeral(where);
  if (result.is_ok()) {
    const size_t bytes_read = result.value().size();
    read_limit_ = std::max(read_limit_, where.offset + bytes_read);
  }
  return result;
}

// The returned view is only guaranteed valid until the next call.  In
// fact, it stays valid possibly for the life of the Mmap and at
// least until shrink_to_fit is called.
fit::result<Error, ByteView> DumpFile::Mmap::ReadEphemeral(FileRange where) {
  ByteView data{reinterpret_cast<std::byte*>(data_), size_};
  data = data.substr(where.offset, where.size);
  if (data.empty()) {
    return TruncatedDump();
  }
  return fit::ok(data);
}

// This never allows EOF since the size is always known and reading past
// EOF should never be attempted.
fit::result<Error, ByteView> DumpFile::Mmap::ReadProbe(FileRange where) {
  return ReadEphemeral(where);
}

// All the data that will be read has been read.
void DumpFile::Mmap::shrink_to_fit() {
  const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  const size_t last_page = size_ & -page_size;
  if (read_limit_ <= last_page) {
    // Unmap the file pages we never looked at with ReadPermanent.
    const size_t limit_page = (read_limit_ + page_size - 1) & -page_size;
    const size_t trim = last_page + page_size - limit_page;
    munmap(static_cast<std::byte*>(data_) + limit_page, trim);
    size_ = limit_page;
  }
}

DumpFile::Mmap::~Mmap() {
  if (size_ > 0) {
    munmap(data_, size_);
  }
}

}  // namespace zxdump::internal
