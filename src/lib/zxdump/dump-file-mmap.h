// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_DUMP_FILE_MMAP_H_
#define SRC_LIB_ZXDUMP_DUMP_FILE_MMAP_H_

#include <lib/zxdump/types.h>

#include "dump-file.h"

namespace zxdump::internal {

// A dump file mapped in wholesale.
class DumpFile::Mmap : public DumpFile {
 public:
  Mmap() = delete;

  Mmap(void* data, size_t size) : data_{data}, size_{size} {}

  Mmap(const Mmap&) = delete;
  Mmap& operator=(const Mmap& other) = delete;

  Mmap(Mmap&& other) noexcept { *this = std::move(other); }

  Mmap& operator=(Mmap&& other) noexcept {
    std::swap(read_limit_, other.read_limit_);
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    return *this;
  }

  size_t size() const override { return size_; }

  // The returned view is valid for the life of the Mmap.
  fit::result<Error, ByteView> ReadPermanent(FileRange where) override;

  // The returned view is only guaranteed valid until the next call.  In
  // fact, it stays valid possibly for the life of the Mmap and at
  // least until shrink_to_fit is called.
  fit::result<Error, ByteView> ReadEphemeral(FileRange where) override;

  // This never allows EOF since the size is always known and reading past
  // EOF should never be attempted.
  fit::result<Error, ByteView> ReadProbe(FileRange where) override;

  // All the data that will be read has been read.
  void shrink_to_fit() override;

  ~Mmap() override;

 private:
  uint64_t read_limit_ = 0;
  void* data_;
  size_t size_;
};

}  // namespace zxdump::internal

#endif  // SRC_LIB_ZXDUMP_DUMP_FILE_MMAP_H_
