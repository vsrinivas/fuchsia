// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_H_

#include <lib/zx/status.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>
#include <optional>

#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <zstd/zstd.h>
#include <zstd/zstd_seekable.h>

#include "compressor.h"
#include "decompressor.h"
#include "seekable-decompressor.h"

namespace blobfs {

struct ZSTDSeekableHeader {
  uint64_t archive_size;
};

constexpr size_t kZSTDSeekableHeaderSize = sizeof(ZSTDSeekableHeader);
constexpr unsigned kZSTDSeekableMaxFrameSize = 128 * kBlobfsBlockSize;

// Compressor implementation for the zstd seekable format library implemented in
// //third_party/zstd/contrib/seekable_format. The library provides a convenient API for
// random access in zstd archives.
class ZSTDSeekableCompressor : public Compressor {
 public:
  // TODO(markdittmer): This can include `kBlobFlagZSTDCompressed` if a unified envelope format is
  // implemented across (minimally) all ZSTD compression strategies.
  static uint32_t InodeHeaderCompressionFlags() { return kBlobFlagZSTDSeekableCompressed; }

  // Returns an upper bound on the size of the buffer required to store the compressed
  // representation of a blob of size `input_length`.
  static size_t BufferMax(size_t input_length);

  static zx_status_t Create(CompressionSettings settings, size_t input_size,
                            void* compression_buffer, size_t compression_buffer_length,
                            std::unique_ptr<ZSTDSeekableCompressor>* out);
  ~ZSTDSeekableCompressor();

  ////////////////////////////////////////
  // Compressor interface
  size_t Size() const final;
  zx_status_t Update(const void* input_data, size_t input_length) final;
  zx_status_t End() final;

 private:
  // Writes up to `kZSTDSeekableHeaderSize` bytes from the beginning of `buf` from `header`.
  // @param buf Pointer to buffer that is to contain <header><zstd seekable archive>.
  // @param buf_size Size of `buf` in bytes.
  // @param header Header values to write.
  // It is the responsibility of any code writing the zstd seekable archive to `buf` to skip the
  // first `kZSTDSeekableHeaderSize` bytes before writing the archive contents. This is generally an
  // implementation detail invoked by other public methods, but is public to enable test
  // environments to write syntactically correct headers via the same code code path used by
  // `ZSTDSeekableCompressor`.
  static zx_status_t WriteHeader(void* buf, size_t buf_size, ZSTDSeekableHeader header);

  ZSTDSeekableCompressor(ZSTD_seekable_CStream* stream, void* compression_buffer,
                         size_t compression_buffer_length);

  ZSTD_seekable_CStream* stream_ = nullptr;
  ZSTD_outBuffer output_ = {};

  DISALLOW_COPY_ASSIGN_AND_MOVE(ZSTDSeekableCompressor);
};

class ZSTDSeekableDecompressor : public Decompressor, public SeekableDecompressor {
 public:
  ZSTDSeekableDecompressor() = default;
  DISALLOW_COPY_ASSIGN_AND_MOVE(ZSTDSeekableDecompressor);

  zx_status_t DecompressArchive(void* uncompressed_buf, size_t* uncompressed_size,
                                const void* compressed_buf, size_t compressed_size, size_t offset);

  // Decompressor implementation.
  zx_status_t Decompress(void* uncompressed_buf, size_t* uncompressed_size,
                         const void* compressed_buf, const size_t max_compressed_size) final;

  // SeekableDecompressor implementation.
  zx_status_t DecompressRange(void* uncompressed_buf, size_t* uncompressed_size,
                              const void* compressed_buf, size_t max_compressed_size,
                              size_t offset) final;
  zx::status<CompressionMapping> MappingForDecompressedRange(size_t offset, size_t len) final {
    // TODO(markdittmer): Implement.
    ZX_ASSERT(false);
    return zx::error(ZX_ERR_INTERNAL);
  }

  // Reads up to `kZSTDSeekableHeaderSize` bytes from the beginning of `buf` into `header`.
  // @param buf Pointer to buffer that is to contain <header><zstd seekable archive>.
  // @param buf_size Size of `buf` in bytes.
  // @param header Header struct in which to store values.
  static zx_status_t ReadHeader(const void* buf, size_t buf_size, ZSTDSeekableHeader* header);
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_ZSTD_SEEKABLE_H_
