// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_H_

#include <zircon/types.h>

#include <memory>

#include <zstd/zstd.h>
#include <zstd/zstd_seekable.h>

#include "compressor.h"

namespace blobfs {

struct ZSTDSeekableHeader {
  uint64_t archive_size;
};

constexpr size_t kZSTDSeekableHeaderSize = sizeof(ZSTDSeekableHeader);

// Compressor implementation for the zstd seekable format library implemented in
// //third_party/zstd/contrib/seekable_format. The library provides a convenient API for
// random access in zstd archives.
class ZSTDSeekableCompressor : public Compressor {
 public:
  // Returns an upper bound on the size of the buffer required to store the compressed
  // representation of a blob of size `input_length`.
  static size_t BufferMax(size_t input_length);

  // Writes up to `kZSTDSeekableHeaderSize` bytes from the beginning of `buf` from `header`.
  // @param buf Pointer to buffer that is to contain <header><zstd seekable archive>.
  // @param buf_size Size of `buf` in bytes.
  // @param header Header values to write.
  // It is the responsibility of any code writing the zstd seekable archive to `buf` to skip the
  // first `kZSTDSeekableHeaderSize` bytes before writing the archive contents. This is generally an
  // implementation detail invoked by other public methods, but is public to enable test
  // environments to write syntactically correct headers via the same code code path used by
  // `ZSTDSeekableCompressor`.
  static zx_status_t WriteZSTDSeekableHeader(void* buf, size_t buf_size, ZSTDSeekableHeader header);

  static zx_status_t Create(size_t input_size, void* compression_buffer,
                            size_t compression_buffer_length,
                            std::unique_ptr<ZSTDSeekableCompressor>* out);
  ~ZSTDSeekableCompressor();

  ////////////////////////////////////////
  // Compressor interface
  size_t Size() const final;
  zx_status_t Update(const void* input_data, size_t input_length) final;
  zx_status_t End() final;

 private:
  ZSTDSeekableCompressor(ZSTD_seekable_CStream* stream, void* compression_buffer,
                         size_t compression_buffer_length);

  ZSTD_seekable_CStream* stream_ = nullptr;
  ZSTD_outBuffer output_ = {};

  DISALLOW_COPY_ASSIGN_AND_MOVE(ZSTDSeekableCompressor);
};

// TODO(markdittmer): Encapsulate decompression functions in testable decompressor class.

// Reads up to `kZSTDSeekableHeaderSize` bytes from the beginning of `buf` into `header`.
// @param buf Pointer to buffer that is to contain <header><zstd seekable archive>.
// @param buf_size Size of `buf` in bytes.
// @param header Header struct in which to store values.
zx_status_t ReadZSTDSeekableHeader(const void* buf, size_t buf_size, ZSTDSeekableHeader* header);

// Decompress the source buffer into the target buffer, starting at _uncompressed_ `offset`, until
// either the source is drained or the target is filled (or both). The source buffer is interpreted
// as a complete zstd seekable archive (with _no_ padding in the source buffer).
zx_status_t ZSTDSeekableDecompressArchive(void* target_buf, size_t* target_size,
                                          const void* src_buf, size_t src_size, size_t offset);

// Decompress the source buffer into the target buffer, starting at _uncompressed_ `offset`, until
// either the source is drained or the target is filled (or both). The source buffer is interpreted
// as a `ZSTDSeekableCompressor`-defined blob that may contain additional metadata beyond a zstd
// seekable archive.
zx_status_t ZSTDSeekableDecompressBytes(void* target_buf, size_t* target_size, const void* src_buf,
                                        size_t src_size, size_t offset);

// Decompress the source buffer into the target buffer, until either the source is drained or
// the target is filled (or both). This is equivalent to:
// `ZSTDSeekableDecompressBytes(target_buf, target_size, src_buf, src_size, 0)`.
zx_status_t ZSTDSeekableDecompress(void* target_buf, size_t* target_size, const void* src_buf,
                                   size_t src_size);

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_H_
