// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/streaming_chunked_decompressor.h"

#include <algorithm>

#include "src/storage/blobfs/compression/chunked.h"

namespace {

const size_t kSystemPageSize = zx_system_get_page_size();

}

namespace blobfs {

zx::result<std::unique_ptr<StreamingChunkedDecompressor>> StreamingChunkedDecompressor::Create(
    DecompressorCreatorConnector& connector, const chunked_compression::SeekTable& seek_table,
    StreamingChunkedDecompressor::StreamCallback stream_callback) {
  ZX_DEBUG_ASSERT(stream_callback != nullptr);
  zx_status_t status;

  fzl::OwnedVmoMapper decompression_buff;
  zx::vmo compression_buff;

  if (seek_table.Entries().empty()) {
    return zx::error(ZX_ERR_INVALID_ARGS);
  }
  using chunked_compression::SeekTableEntry;
  SeekTableEntry* largest_decompressed_entry =
      std::max_element(seek_table.Entries().begin(), seek_table.Entries().end(),
                       [](const SeekTableEntry& a, const SeekTableEntry& b) {
                         return a.decompressed_size < b.decompressed_size;
                       });

  status = decompression_buff.CreateAndMap(largest_decompressed_entry->decompressed_size,
                                           "blobfs-write-decomp-buff");
  if (status != ZX_OK) {
    return zx::error(status);
  }
  status = zx::vmo::create(seek_table.CompressedSize(), 0, &compression_buff);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  zx::result client_or =
      ExternalDecompressorClient::Create(&connector, decompression_buff.vmo(), compression_buff);
  if (client_or.is_error()) {
    return client_or.take_error();
  }

  std::unique_ptr<StreamingChunkedDecompressor> out(new StreamingChunkedDecompressor(
      seek_table, std::move(client_or.value()), std::move(decompression_buff),
      std::move(compression_buff), std::move(stream_callback)));

  return zx::ok(std::move(out));
}

StreamingChunkedDecompressor::StreamingChunkedDecompressor(
    const chunked_compression::SeekTable& seek_table,
    std::unique_ptr<ExternalDecompressorClient> decompressor_client,
    fzl::OwnedVmoMapper decompression_buff, zx::vmo compression_buff,
    StreamingChunkedDecompressor::StreamCallback stream_callback)
    : seek_table_(seek_table),
      decompressor_client_(std::move(decompressor_client)),
      decompressor_(decompressor_client_.get(), CompressionAlgorithm::kChunked),
      decompression_buff_(std::move(decompression_buff)),
      compression_buff_(std::move(compression_buff)),
      stream_callback_(std::move(stream_callback)) {}

zx::result<> StreamingChunkedDecompressor::Update(cpp20::span<const uint8_t> data) {
  if (compressed_bytes_ + data.size_bytes() > seek_table_.CompressedSize()) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }
  if (curr_entry_ == seek_table_.Entries().end()) {
    return zx::error(ZX_ERR_OUT_OF_RANGE);
  }

  compression_buff_.write(data.data(), compressed_bytes_, data.size_bytes());
  compressed_bytes_ += data.size_bytes();

  // If we don't have enough bytes to decode the next seek table entry, wait for more data.
  if (compressed_bytes_ < curr_entry_->compressed_offset + curr_entry_->compressed_size) {
    return zx::ok();
  }

  while (curr_entry_ != seek_table_.Entries().end() &&
         compressed_bytes_ >= curr_entry_->compressed_offset + curr_entry_->compressed_size) {
    zx_status_t status =
        decompressor_.DecompressRange(curr_entry_->compressed_offset, curr_entry_->compressed_size,
                                      curr_entry_->decompressed_size);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    zx::result result = stream_callback_(
        {static_cast<const uint8_t*>(decompression_buff_.start()), curr_entry_->decompressed_size});
    if (result.is_error()) {
      return result.take_error();
    }
    ++curr_entry_;
  }

  // Decommit unused pages from compressed buffer VMO.
  if (curr_entry_ != seek_table_.Entries().end()) {
    // We have more seek table entries to process, decommit all pages behind the current entry.
    zx_status_t status = compression_buff_.op_range(
        ZX_VMO_OP_DECOMMIT, 0, fbl::round_down(curr_entry_->compressed_offset, kSystemPageSize),
        nullptr, 0);
    if (status != ZX_OK) {
      return zx::error(status);
    }
  } else {
    // We processed all seek table entries, we can just destroy the buffer VMOs.
    compression_buff_.reset();
    decompression_buff_.Reset();
  }
  return zx::ok();
}

}  // namespace blobfs
