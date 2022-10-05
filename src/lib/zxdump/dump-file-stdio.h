// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_DUMP_FILE_STDIO_H_
#define SRC_LIB_ZXDUMP_DUMP_FILE_STDIO_H_

#include <lib/zxdump/types.h>

#include <cstdio>
#include <forward_list>
#include <memory>

#include "dump-file.h"

namespace zxdump::internal {

// A dump file read on demand from the fd, using stdio for buffering.
class DumpFile::Stdio : public DumpFile {
 public:
  Stdio() = delete;

  Stdio(Stdio&&) = default;

  Stdio& operator=(Stdio&&) = default;

  Stdio(FILE* stream, size_t size) : stream_(stream), size_(size) {
    if (size_ == 0) {
      shrink_to_fit();
    }
  }

  ~Stdio() override;

  size_t size() const override { return size_; }

  // Return the available subset of the requested data, a view valid for the
  // life of the Stdio.
  fit::result<Error, ByteView> ReadPermanent(FileRange where) override;

  // Return the available subset of the requested data, a view valid only
  // until the next call to this method.  The returned data might be less
  // than what's requested if EOF is reached.
  fit::result<Error, ByteView> ReadProbe(FileRange where) override;

  // Return the available subset of the requested data, a view valid only
  // until the next call to this method.  The data must be present.
  fit::result<Error, ByteView> ReadEphemeral(FileRange where) override;

  void shrink_to_fit() override { stream_.reset(); }

 private:
  struct StdioCloser {
    void operator()(FILE* stream) const { fclose(stream); }
  };

  fit::result<Error, Buffer> Read(FileRange where);

  std::unique_ptr<FILE, StdioCloser> stream_;
  std::forward_list<Buffer> keepalive_;
  Buffer ephemeral_buffer_;
  FileRange ephemeral_buffer_range_{};
  size_t size_ = 0;
  size_t pos_ = 0;
  bool is_pipe_ = false;
};

}  // namespace zxdump::internal

#endif  // SRC_LIB_ZXDUMP_DUMP_FILE_STDIO_H_
