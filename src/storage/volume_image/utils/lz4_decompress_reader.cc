// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/lz4_decompress_reader.h"

#include <lib/fpromise/result.h>
#include <string.h>

#include <cstdint>
#include <memory>

#include "src/storage/volume_image/utils/lz4_decompressor.h"

namespace storage::volume_image {

fpromise::result<void, std::string> Lz4DecompressReader::Initialize(
    uint64_t max_buffer_size) const {
  context_.decompressed_data.resize(max_buffer_size, 0);
  context_.decompressed_offset = offset_;
  context_.decompressed_length = 0;

  context_.compressed_data.resize(max_buffer_size, 0);
  context_.compressed_offset = offset_;

  context_.decompressor = std::make_unique<Lz4Decompressor>(max_buffer_size);
  context_.hint = std::nullopt;

  return context_.decompressor->Prepare(
      [this](auto decompressed_data) { return this->DecompressionHandler(decompressed_data); });
}

fpromise::result<void, std::string> Lz4DecompressReader::DecompressionHandler(
    cpp20::span<const uint8_t> decompressed_data) const {
  memcpy(context_.decompressed_data.data(), decompressed_data.data(), decompressed_data.size());
  context_.decompressed_offset += context_.decompressed_length;
  context_.decompressed_length = decompressed_data.size();

  return fpromise::ok();
}

fpromise::result<void, std::string> Lz4DecompressReader::Seek(uint64_t offset) const {
  // Offset in uncompressed area.
  if (offset < offset_) {
    return fpromise::ok();
  }

  if (offset < context_.decompressed_offset) {
    if (auto result = Initialize(context_.decompressed_data.size()); result.is_error()) {
      return result.take_error_result();
    }
  }

  // Offset is in range.
  auto offset_in_range = [&]() {
    return context_.decompressed_length > 0 && offset >= context_.decompressed_offset &&
           offset < context_.decompressed_offset + context_.decompressed_length;
  };

  auto end_of_compressed_data = [&]() {
    return context_.compressed_offset == compressed_reader_->length();
  };

  auto end_of_frame = [&]() { return context_.hint.has_value() && context_.hint.value() == 0; };

  // Decompress until offset is in range.
  while (!offset_in_range() && !end_of_frame() && !end_of_compressed_data()) {
    if (auto result = NextDecompressedChunk(); result.is_error()) {
      return result;
    }
  }

  if (!offset_in_range() && (end_of_frame() || end_of_compressed_data())) {
    return fpromise::error("Reached end of compressed data before reaching offset.");
  };
  return fpromise::ok();
}

fpromise::result<void, std::string> Lz4DecompressReader::NextDecompressedChunk() const {
  auto read_view = cpp20::span<uint8_t>(context_.compressed_data);
  uint64_t remaining_compressed_bytes = compressed_reader_->length() - context_.compressed_offset;

  if (read_view.size() > remaining_compressed_bytes) {
    read_view = read_view.subspan(0, remaining_compressed_bytes);
  }

  if (context_.hint.has_value() && read_view.size() > context_.hint.value()) {
    read_view = read_view.subspan(0, context_.hint.value());
  }

  if (auto result = compressed_reader_->Read(context_.compressed_offset, read_view);
      result.is_error()) {
    return result;
  }

  auto decompress_result = context_.decompressor->Decompress(read_view);
  if (decompress_result.is_error()) {
    return decompress_result.take_error_result();
  }

  auto [hint, consumed_bytes] = decompress_result.value();
  context_.hint = hint;
  context_.compressed_offset += consumed_bytes;
  return fpromise::ok();
}

fpromise::result<void, std::string> Lz4DecompressReader::Read(uint64_t offset,
                                                              cpp20::span<uint8_t> buffer) const {
  // Base recursion case.
  if (buffer.empty()) {
    return fpromise::ok();
  }

  // Attempting to read out of the uncompressed range.
  if (offset < offset_) {
    uint64_t uncompressed_bytes = offset_ - offset;
    uint64_t uncompressed_bytes_to_copy =
        std::min(static_cast<uint64_t>(buffer.size()), uncompressed_bytes);
    if (auto result =
            compressed_reader_->Read(offset, buffer.subspan(0, uncompressed_bytes_to_copy));
        result.is_error()) {
      return result;
    }

    offset += uncompressed_bytes_to_copy;
    buffer = buffer.subspan(uncompressed_bytes_to_copy);
    if (buffer.empty()) {
      return fpromise::ok();
    }
  }

  while (!buffer.empty()) {
    if (auto result = Seek(offset); result.is_error()) {
      return result;
    }

    // Now the data is in the buffer, or at least some of it.
    uint64_t decompressed_buffer_offset = offset - context_.decompressed_offset;
    uint64_t decompressed_buffer_bytes = context_.decompressed_length - decompressed_buffer_offset;
    uint64_t decompressed_bytes_to_copy =
        std::min(static_cast<uint64_t>(buffer.size()), decompressed_buffer_bytes);
    memcpy(buffer.data(), context_.decompressed_data.data() + decompressed_buffer_offset,
           decompressed_bytes_to_copy);

    offset += decompressed_bytes_to_copy;
    buffer = buffer.subspan(decompressed_bytes_to_copy);
  }
  return fpromise::ok();
}

}  // namespace storage::volume_image
