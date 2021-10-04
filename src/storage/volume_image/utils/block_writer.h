// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_BLOCK_WRITER_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_BLOCK_WRITER_H_

#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>

#include <string_view>
#include <vector>

#include "src/storage/volume_image/utils/reader.h"
#include "src/storage/volume_image/utils/writer.h"

namespace storage::volume_image {

// Adapts the Writer API to a block device-like API. That is, converts unaligned writes into aligned
// ones, by reading back the aligned data and patching the unaligned data into it.
class BlockWriter final : public Writer {
 public:
  BlockWriter(uint64_t block_size, uint64_t block_count, std::unique_ptr<Reader> reader,
              std::unique_ptr<Writer> writer)
      : block_size_(block_size),
        block_count_(block_count),
        writer_(std::move(writer)),
        reader_(std::move(reader)) {
    block_buffer_.resize(block_size_, 0);
  }
  BlockWriter(const BlockWriter&) = delete;
  BlockWriter(BlockWriter&&) = default;
  BlockWriter& operator=(const BlockWriter&) = delete;
  BlockWriter& operator=(BlockWriter&&) = default;

  // On success data backing this writer is updated at [|offset|, |offset| +
  // |buffer.size()|] to |buffer|.
  //
  // On error the returned result to contains a string describing the error.
  fpromise::result<void, std::string> Write(uint64_t offset,
                                            cpp20::span<const uint8_t> buffer) final;

 private:
  // Used to define the block alignment of the resource.
  uint64_t block_size_ = 0;

  // Number of block availble.
  uint64_t block_count_ = 0;

  // block size buffer, to read unaligned chunks.
  std::vector<uint8_t> block_buffer_;

  // Actual writer that owns the block resource.
  std::unique_ptr<Writer> writer_ = nullptr;

  // To support writing unaligned portions, we need to read the data back.
  std::unique_ptr<Reader> reader_ = nullptr;
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_BLOCK_WRITER_H_
