// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_BLOB_DATA_PRODUCER_H_
#define SRC_STORAGE_BLOBFS_BLOB_DATA_PRODUCER_H_

#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>

#include <memory>

namespace blobfs {

class BlobCompressor;
class SeekableDecompressor;

// BlobDataProducer is an abstact class that is used when writing blobs. It produces data (see the
// Consume method) which is then to be written to the device.
class BlobDataProducer {
 public:
  // The number of bytes remaining for this producer.
  virtual uint64_t GetRemainingBytes() const = 0;

  // Producers must be able to accommodate zero padding up to kBlobfsBlockSize if it would be
  // required i.e. if the last span returned is not a whole block size, it must point to a buffer
  // that can be extended with zero padding (which will be done by the caller).
  virtual zx::result<cpp20::span<const uint8_t>> Consume(uint64_t max) = 0;

  // Subclasses should return true if the next call to Consume would invalidate data returned by
  // previous calls to Consume.
  virtual bool NeedsFlush() const { return false; }
};

// A simple producer that just vends data from a supplied span.
class SimpleBlobDataProducer : public BlobDataProducer {
 public:
  explicit SimpleBlobDataProducer(cpp20::span<const uint8_t> data) : data_(data) {}

  // BlobDataProducer implementation:
  uint64_t GetRemainingBytes() const override;
  zx::result<cpp20::span<const uint8_t>> Consume(uint64_t max) override;

 private:
  cpp20::span<const uint8_t> data_;
};

// Merges two producers together with optional padding between them. If there is padding, we
// require the second producer to be able to accommodate padding at the beginning up to
// kBlobfsBlockSize i.e. the first span it returns must point to a buffer that can be prepended with
// up to kBlobfsBlockSize bytes. Both producers should be able to accommodate padding at the end if
// it would be required.
class MergeBlobDataProducer : public BlobDataProducer {
 public:
  MergeBlobDataProducer(BlobDataProducer& first, BlobDataProducer& second, size_t padding);

  // BlobDataProducer implementation:
  uint64_t GetRemainingBytes() const override;
  zx::result<cpp20::span<const uint8_t>> Consume(uint64_t max) override;
  bool NeedsFlush() const override;

 private:
  BlobDataProducer& first_;
  BlobDataProducer& second_;
  size_t padding_;
};

// A producer that allows us to write uncompressed data by decompressing data.  This is used when we
// discover that it is not profitable to compress a blob.  It decompresses into a temporary buffer.
class DecompressBlobDataProducer : public BlobDataProducer {
 public:
  static zx::result<DecompressBlobDataProducer> Create(BlobCompressor& compressor,
                                                       uint64_t decompressed_size);

  // BlobDataProducer implementation:
  uint64_t GetRemainingBytes() const override;
  zx::result<cpp20::span<const uint8_t>> Consume(uint64_t max) override;
  bool NeedsFlush() const override;

 private:
  DecompressBlobDataProducer(std::unique_ptr<SeekableDecompressor> decompressor,
                             uint64_t decompressed_size, size_t buffer_size,
                             const void* compressed_data_start);

  // Decompress into the temporary buffer.
  zx_status_t Decompress();

  std::unique_ptr<SeekableDecompressor> decompressor_;

  // The total number of decompressed bytes left to decompress.
  uint64_t decompressed_remaining_;

  // A temporary buffer we use to decompress into.
  std::unique_ptr<uint8_t[]> buffer_;

  // The size of the temporary buffer.
  const size_t buffer_size_;

  // Pointer to the first byte of compressed data.
  const uint8_t* compressed_data_start_;

  // The current offset of decompressed bytes.
  uint64_t decompressed_offset_ = 0;

  // The current offset in the temporary buffer indicate what to return on the next call to Consume.
  size_t buffer_offset_ = 0;

  // The number of bytes available in the temporary buffer.
  size_t buffer_avail_ = 0;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_BLOB_DATA_PRODUCER_H_
