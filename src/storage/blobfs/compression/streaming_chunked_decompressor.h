// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_COMPRESSION_STREAMING_CHUNKED_DECOMPRESSOR_H_
#define SRC_STORAGE_BLOBFS_COMPRESSION_STREAMING_CHUNKED_DECOMPRESSOR_H_

#ifndef __Fuchsia__
static_assert(false, "Fuchsia only header");
#endif

#include <lib/fit/function.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>

#include "src/lib/chunked-compression/chunked-archive.h"
#include "src/storage/blobfs/compression/external_decompressor.h"

namespace blobfs {

// Streaming decompressor for the chunked format backed by an exernal seekable decompressor.
// Data is streamed into the given callback function when it's available by decoding each
// seek table in order. Once decompressed, unused ranges of the compressed data are decommitted.
class StreamingChunkedDecompressor {
 public:
  // Type of callback used to handle streaming data as it is decompressed.
  using StreamCallback = fit::function<zx::result<>(cpp20::span<const uint8_t>)>;

  static zx::result<std::unique_ptr<StreamingChunkedDecompressor>> Create(
      DecompressorCreatorConnector& connector, const chunked_compression::SeekTable& seek_table,
      StreamCallback stream_callback);

  // Add more data to the internal state of the decompressor.
  zx::result<> Update(cpp20::span<const uint8_t> data);

 private:
  StreamingChunkedDecompressor(const chunked_compression::SeekTable& seek_table,
                               std::unique_ptr<ExternalDecompressorClient> decompressor_client,
                               fzl::OwnedVmoMapper decompression_buff, zx::vmo compression_buff,
                               StreamingChunkedDecompressor::StreamCallback stream_callback);

  const chunked_compression::SeekTable& seek_table_;
  std::unique_ptr<ExternalDecompressorClient> decompressor_client_;
  ExternalSeekableDecompressor decompressor_;
  /// Buffer to decompress data into. Must be large enough to hold the biggest seek table entry.
  fzl::OwnedVmoMapper decompression_buff_;
  /// Buffer to store compressed data as it is received. Unused data is decomitted.
  zx::vmo compression_buff_;
  StreamCallback stream_callback_;

  /// Number of bytes of the compressed archive passed to Update() thus far, including the header.
  size_t compressed_bytes_ = 0;

  /// Current seek table entry we need to decompress.
  const chunked_compression::SeekTableEntry* curr_entry_ = seek_table_.Entries().begin();
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_COMPRESSION_STREAMING_CHUNKED_DECOMPRESSOR_H_
