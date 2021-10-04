// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_DECOMPRESS_READER_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_DECOMPRESS_READER_H_

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "src/storage/volume_image/utils/lz4_decompressor.h"
#include "src/storage/volume_image/utils/reader.h"

namespace storage::volume_image {

// Provides a decompressed view of the underlying compressed data.
class Lz4DecompressReader final : public Reader {
 public:
  // Default size for |StreamContext| buffers.
  static constexpr uint64_t kMaxBufferSize = 2 * (1 << 20);

  // Lz4DecompressReader will decompress data starting at |offset|. That is the compressed data is
  // embedded in |compressed_reader| and the first compressed byte is at |offset|.
  Lz4DecompressReader(uint64_t offset, uint64_t decompressed_length,
                      std::shared_ptr<Reader> compressed_reader)
      : offset_(offset),
        length_(decompressed_length),
        compressed_reader_(std::move(compressed_reader)) {}

  // Initializes the underlying |StreamContext|.
  fpromise::result<void, std::string> Initialize(uint64_t max_buffer_size = kMaxBufferSize) const;

  // Returns the number of bytes readable from this reader.
  uint64_t length() const final { return length_; }

  // On success data at [|offset|, |offset| + |buffer.size()|] are read into
  // |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  fpromise::result<void, std::string> Read(uint64_t offset,
                                           cpp20::span<uint8_t> buffer) const final;

 private:
  fpromise::result<void, std::string> DecompressionHandler(
      cpp20::span<const uint8_t> decompressed_data) const;

  fpromise::result<void, std::string> Seek(uint64_t offset) const;

  fpromise::result<void, std::string> NextDecompressedChunk() const;

  // Describes the current state of the decompression stream.
  struct StreamContext {
    std::vector<uint8_t> compressed_data;
    uint64_t compressed_offset = 0;

    std::vector<uint8_t> decompressed_data;
    uint64_t decompressed_offset = 0;
    uint64_t decompressed_length = 0;

    std::optional<uint64_t> hint = std::nullopt;

    std::unique_ptr<Lz4Decompressor> decompressor = nullptr;
  };

  // Reinitializes the streaming context.
  fpromise::result<void, std::string> ResetStreamContext();

  uint64_t offset_ = 0;
  uint64_t length_ = 0;
  std::shared_ptr<Reader> compressed_reader_ = nullptr;

  // Mutable since this will never change the contents of a given range in the exposed view.
  mutable StreamContext context_;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_LZ4_DECOMPRESS_READER_H_
