// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/blob_data_producer.h"

#include <lib/syslog/cpp/macros.h>

#include "src/storage/blobfs/compression/blob_compressor.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/seekable_decompressor.h"

namespace blobfs {

uint64_t SimpleBlobDataProducer::GetRemainingBytes() const { return data_.size(); }

zx::result<cpp20::span<const uint8_t>> SimpleBlobDataProducer::Consume(uint64_t max) {
  auto result = data_.subspan(0, std::min(max, data_.size()));
  data_ = data_.subspan(result.size());
  return zx::ok(result);
}

MergeBlobDataProducer::MergeBlobDataProducer(BlobDataProducer& first, BlobDataProducer& second,
                                             size_t padding)
    : first_(first), second_(second), padding_(padding) {
  ZX_ASSERT_MSG(padding_ < kBlobfsBlockSize, "Padding size:%lu more than blobfs block size: %lu",
                padding_, kBlobfsBlockSize);
}

uint64_t MergeBlobDataProducer::GetRemainingBytes() const {
  return first_.GetRemainingBytes() + padding_ + second_.GetRemainingBytes();
}

zx::result<cpp20::span<const uint8_t>> MergeBlobDataProducer::Consume(uint64_t max) {
  if (first_.GetRemainingBytes() > 0) {
    auto data = first_.Consume(max);
    if (data.is_error()) {
      return data;
    }

    // Deal with data returned that isn't a multiple of the block size.
    const size_t alignment = data->size() % kBlobfsBlockSize;
    if (alignment > 0) {
      // First, add any padding that might be required.
      const size_t to_pad = std::min(padding_, kBlobfsBlockSize - alignment);
      uint8_t* p = const_cast<uint8_t*>(data->end());
      memset(p, 0, to_pad);
      p += to_pad;
      data.value() = cpp20::span(data->data(), data->size() + to_pad);
      padding_ -= to_pad;

      // If we still don't have a full block, fill the block with data from the second producer.
      const size_t alignment = data->size() % kBlobfsBlockSize;
      if (alignment > 0) {
        auto data2 = second_.Consume(kBlobfsBlockSize - alignment);
        if (data2.is_error())
          return data2;
        memcpy(p, data2->data(), data2->size());
        data.value() = cpp20::span(data->data(), data->size() + data2->size());
      }
    }
    return data;
  } else {
    auto data = second_.Consume(max - padding_);
    if (data.is_error())
      return data;

    // If we have some padding, prepend zeroed data.
    if (padding_ > 0) {
      data.value() = cpp20::span(data->data() - padding_, data->size() + padding_);
      memset(const_cast<uint8_t*>(data->data()), 0, padding_);
      padding_ = 0;
    }
    return data;
  }
}

bool MergeBlobDataProducer::NeedsFlush() const {
  return first_.NeedsFlush() || second_.NeedsFlush();
}

DecompressBlobDataProducer::DecompressBlobDataProducer(
    std::unique_ptr<SeekableDecompressor> decompressor, uint64_t decompressed_size,
    size_t buffer_size, const void* compressed_data_start)
    : decompressor_(std::move(decompressor)),
      decompressed_remaining_(decompressed_size),
      buffer_(std::make_unique<uint8_t[]>(buffer_size)),
      buffer_size_(buffer_size),
      compressed_data_start_(static_cast<const uint8_t*>(compressed_data_start)) {}

zx::result<DecompressBlobDataProducer> DecompressBlobDataProducer::Create(
    BlobCompressor& compressor, uint64_t decompressed_size) {
  ZX_ASSERT_MSG(compressor.algorithm() == CompressionAlgorithm::kChunked, "%u",
                compressor.algorithm());
  std::unique_ptr<SeekableDecompressor> decompressor;
  const size_t compressed_size = compressor.Size();
  if (zx_status_t status = SeekableChunkedDecompressor::CreateDecompressor(
          cpp20::span(static_cast<const uint8_t*>(compressor.Data()), compressed_size),
          compressed_size, &decompressor);
      status != ZX_OK) {
    return zx::error(status);
  }

  return zx::ok(DecompressBlobDataProducer(
      std::move(decompressor), decompressed_size,
      fbl::round_up(131'072ul, compressor.compressor().GetChunkSize()), compressor.Data()));
}

uint64_t DecompressBlobDataProducer::GetRemainingBytes() const {
  return decompressed_remaining_ + buffer_avail_;
}

zx::result<cpp20::span<const uint8_t>> DecompressBlobDataProducer::Consume(uint64_t max) {
  if (buffer_avail_ == 0) {
    if (zx_status_t status = Decompress(); status != ZX_OK) {
      return zx::error(status);
    }
  }
  cpp20::span result(buffer_.get() + buffer_offset_, std::min(buffer_avail_, max));
  buffer_offset_ += result.size();
  buffer_avail_ -= result.size();
  return zx::ok(result);
}

// Return true if previous data would be invalidated by the next call to Consume.
bool DecompressBlobDataProducer::NeedsFlush() const {
  return buffer_offset_ > 0 && buffer_avail_ == 0;
}

// Decompress into the temporary buffer.
zx_status_t DecompressBlobDataProducer::Decompress() {
  size_t decompressed_length = std::min(buffer_size_, decompressed_remaining_);
  auto mapping_or = decompressor_->MappingForDecompressedRange(
      decompressed_offset_, decompressed_length, std::numeric_limits<size_t>::max());
  if (mapping_or.is_error()) {
    return mapping_or.error_value();
  }
  ZX_ASSERT_MSG(
      mapping_or.value().decompressed_offset == decompressed_offset_,
      "mapping_or.value().decompressed_offset :%lu not equal to decompressed_offset_ :%lu",
      mapping_or.value().decompressed_offset, decompressed_offset_);
  ZX_ASSERT_MSG(mapping_or.value().decompressed_length == decompressed_length,
                "mapping_or.value().decompressed_length :%lu not equal to decompressed_length: %lu",
                mapping_or.value().decompressed_length, decompressed_length);
  if (zx_status_t status = decompressor_->DecompressRange(
          buffer_.get(), &decompressed_length,
          compressed_data_start_ + mapping_or.value().compressed_offset,
          mapping_or.value().compressed_length, mapping_or.value().decompressed_offset);
      status != ZX_OK) {
    return status;
  }
  ZX_ASSERT_MSG(mapping_or.value().decompressed_length == decompressed_length,
                "mapping_or.value().decompressed_length :%lu not equal to decompressed_length:%lu",
                mapping_or.value().decompressed_length, decompressed_length);
  buffer_avail_ = decompressed_length;
  buffer_offset_ = 0;
  decompressed_remaining_ -= decompressed_length;
  decompressed_offset_ += decompressed_length;
  return ZX_OK;
}

}  // namespace blobfs
