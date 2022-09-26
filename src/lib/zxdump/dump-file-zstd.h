// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_DUMP_FILE_ZSTD_H_
#define SRC_LIB_ZXDUMP_DUMP_FILE_ZSTD_H_

#include <forward_list>

#include <zstd/zstd.h>

#include "dump-file.h"

namespace zxdump::internal {

static_assert(kHeaderProbeSize >= ZSTD_FRAMEHEADERSIZE_MAX);

// A virtual dump file via streaming decompression from another dump file.
class DumpFile::Zstd : public DumpFile {
 public:
  Zstd() = delete;

  Zstd(DumpFile& file, FileRange where)
      : file_{&file}, file_pos_{where.offset, 0}, ctx_{ZSTD_createDCtx()} {}

  Zstd(Zstd&&) = default;
  Zstd& operator=(Zstd&&) = default;

  size_t size() const override;

  fitx::result<Error, ByteView> ReadProbe(FileRange where) override;

  fitx::result<Error, ByteView> ReadEphemeral(FileRange where) override;

  fitx::result<Error, ByteView> ReadPermanent(FileRange where) override;

  void shrink_to_fit() override;

  // Put some data through the decompressor.
  fitx::result<Error, bool> Pump(ByteView compressed, size_t skip);

 private:
  struct FreeCtx {
    void operator()(ZSTD_DCtx* ctx) const { ZSTD_freeDCtx(ctx); }
  };

  fitx::result<Error, ByteView> Read(FileRange where, bool permanent, bool probe);

  // The read state of the underlying stream: the offset of what's
  // already been read; and the size of what to read next.  When the size
  // is zero that means the decompressor has finished and there is no
  // more to read.
  DumpFile* file_;
  FileRange file_pos_{};

  std::unique_ptr<ZSTD_DCtx, FreeCtx> ctx_;  // Decompressor state.

  // Decompression writes into this buffer, corresponding to a range of the
  // uncompressed file image.  This acts as the ephemeral buffer too.
  Buffer buffer_;
  FileRange buffer_range_{};

  // ReadPermanent results are kept here.
  std::forward_list<Buffer> keepalive_;

  // Occasionally a dangling ephemeral buffer has to be kept alive
  // temporarily when a new buffer is allocated.
  std::forward_list<Buffer> ephemeral_;
};

}  // namespace zxdump::internal

#endif  // SRC_LIB_ZXDUMP_DUMP_FILE_ZSTD_H_
