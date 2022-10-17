// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/owned-vmo-mapper.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/streaming_chunked_decompressor.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/unit/local_decompressor_creator.h"

namespace blobfs {

namespace {

constexpr size_t kTestDataSize = (1 << 24);
constexpr size_t kStreamChunkSize = 1500;
constexpr CompressionSettings kCompressionSettings = {
    .compression_algorithm = CompressionAlgorithm::kChunked,
    .compression_level = 5,
};

class StreamingDecompressorTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Generate some data to use for the test case.
    {
      std::unique_ptr<BlobInfo> blob = GenerateRealisticBlob("", kTestDataSize);
      original_data_ = std::move(blob->data);
      original_data_size_ = blob->size_data;
    }
    // Compress the data.
    {
      std::unique_ptr<ChunkedCompressor> compressor;
      size_t output_limit;
      ASSERT_EQ(ChunkedCompressor::Create(kCompressionSettings, kTestDataSize, &output_limit,
                                          &compressor),
                ZX_OK);

      compressed_data_ = std::vector<uint8_t>(output_limit);
      ASSERT_EQ(compressor->SetOutput(compressed_data_.data(), compressed_data_.size()), ZX_OK);
      ASSERT_EQ(compressor->Update(original_data_.get(), original_data_size_), ZX_OK);
      ASSERT_EQ(compressor->End(), ZX_OK);
    }
    // Parse the resulting seek table.
    {
      chunked_compression::HeaderReader header_reader;
      ASSERT_EQ(header_reader.Parse(compressed_data_.data(), compressed_data_.size(),
                                    compressed_data_.size(), &seek_table_),
                chunked_compression::kStatusOk);
    }
    // Make sure the data we compressed has at least two chunks so we exercise all code paths.
    ASSERT_GE(seek_table_.Entries().size(), 2u);

    zx::result local_decompressor = LocalDecompressorCreator::Create();
    ASSERT_TRUE(local_decompressor.is_ok()) << local_decompressor.status_string();
    local_decompressor_ = std::move(local_decompressor.value());
  }

  const chunked_compression::SeekTable& seek_table() { return seek_table_; }

  cpp20::span<const uint8_t> original_data() const {
    return {original_data_.get(), original_data_size_};
  }

  cpp20::span<const uint8_t> compressed_data() const {
    ZX_ASSERT(seek_table_.SerializedHeaderSize() <= compressed_data_.size());
    return {compressed_data_.data(), seek_table_.CompressedSize()};
  }

  DecompressorCreatorConnector& DecompressorConnector() const {
    return local_decompressor_->GetDecompressorConnector();
  }

 private:
  std::unique_ptr<uint8_t[]> original_data_;
  size_t original_data_size_;
  std::vector<uint8_t> compressed_data_;
  chunked_compression::SeekTable seek_table_;
  std::unique_ptr<LocalDecompressorCreator> local_decompressor_;
};

// Test that the streaming decompressor can handle decompressing the entire file at once.
TEST_F(StreamingDecompressorTest, WholeFile) {
  std::vector<uint8_t> decompressed_data(kTestDataSize);
  size_t decompressed_data_offset = 0;

  auto callback = [&](cpp20::span<const uint8_t> data) -> zx::result<> {
    ZX_ASSERT(decompressed_data_offset + data.size() <= decompressed_data.size());
    std::copy(data.begin(), data.end(), decompressed_data.data() + decompressed_data_offset);
    decompressed_data_offset += data.size();
    return zx::ok();
  };

  zx::result streaming_decompressor = StreamingChunkedDecompressor::Create(
      DecompressorConnector(), seek_table(), std::move(callback));
  ASSERT_TRUE(streaming_decompressor.is_ok()) << streaming_decompressor.status_string();

  zx::result result = streaming_decompressor->Update(compressed_data());
  ASSERT_TRUE(result.is_ok()) << result.status_string();

  ASSERT_EQ(decompressed_data_offset, original_data().size());
  ASSERT_EQ(std::memcmp(decompressed_data.data(), original_data().data(), original_data().size()),
            0);
}

// Test that the streaming decompressor can handle decompressing the file in chunks.
TEST_F(StreamingDecompressorTest, Chunked) {
  std::vector<uint8_t> decompressed_data(kTestDataSize);
  size_t decompressed_data_offset = 0;

  auto callback = [&](cpp20::span<const uint8_t> data) -> zx::result<> {
    ZX_ASSERT(decompressed_data_offset + data.size() <= decompressed_data.size());
    std::copy(data.begin(), data.end(), decompressed_data.data() + decompressed_data_offset);
    decompressed_data_offset += data.size();
    return zx::ok();
  };

  zx::result streaming_decompressor = StreamingChunkedDecompressor::Create(
      DecompressorConnector(), seek_table(), std::move(callback));
  ASSERT_TRUE(streaming_decompressor.is_ok()) << streaming_decompressor.status_string();

  size_t bytes_streamed = 0;
  while (bytes_streamed < compressed_data().size()) {
    size_t bytes_to_stream = std::min(kStreamChunkSize, compressed_data().size() - bytes_streamed);
    zx::result result =
        streaming_decompressor->Update(compressed_data().subspan(bytes_streamed, bytes_to_stream));
    ASSERT_TRUE(result.is_ok()) << result.status_string();
    bytes_streamed += bytes_to_stream;
  }
  ASSERT_EQ(bytes_streamed, compressed_data().size());
  ASSERT_EQ(decompressed_data_offset, original_data().size());
  ASSERT_EQ(std::memcmp(decompressed_data.data(), original_data().data(), original_data().size()),
            0);
}

// Test that we get a failure if we try to add more data to the decompressor past the end.
TEST_F(StreamingDecompressorTest, ExtraDataFails) {
  auto callback = [&](cpp20::span<const uint8_t>) -> zx::result<> { return zx::ok(); };
  zx::result streaming_decompressor = StreamingChunkedDecompressor::Create(
      DecompressorConnector(), seek_table(), std::move(callback));
  ASSERT_TRUE(streaming_decompressor.is_ok()) << streaming_decompressor.status_string();
  zx::result result = streaming_decompressor->Update(compressed_data());
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  // Try to stream in more data past the end of the archive. The actual amount of data doesn't
  // matter, only that we get a failure trying to process more.
  std::vector<uint8_t> extra_data(kStreamChunkSize);
  result = streaming_decompressor->Update({extra_data.data(), extra_data.size()});
  ASSERT_EQ(result.status_value(), ZX_ERR_OUT_OF_RANGE) << result.status_string();
  // We should get the same failure if we try to call Update with an empty span as well.
  result = streaming_decompressor->Update({});
  ASSERT_EQ(result.status_value(), ZX_ERR_OUT_OF_RANGE) << result.status_string();
}

// Test that we can't create a streaming decompressor with an invalid seek table.
TEST_F(StreamingDecompressorTest, InvalidSeekTable) {
  auto callback = [&](cpp20::span<const uint8_t>) -> zx::result<> { return zx::ok(); };
  chunked_compression::SeekTable empty_seek_table{};
  zx::result streaming_decompressor = StreamingChunkedDecompressor::Create(
      DecompressorConnector(), empty_seek_table, std::move(callback));
  ASSERT_EQ(streaming_decompressor.status_value(), ZX_ERR_INVALID_ARGS)
      << streaming_decompressor.status_string();
}

// Test that errors in the stream callback are propagated.
TEST_F(StreamingDecompressorTest, StreamCallbackError) {
  constexpr zx_status_t kTestErrorCode = ZX_ERR_INTERNAL;
  auto callback = [&](cpp20::span<const uint8_t>) -> zx::result<> {
    return zx::error(kTestErrorCode);
  };
  zx::result streaming_decompressor = StreamingChunkedDecompressor::Create(
      DecompressorConnector(), seek_table(), std::move(callback));
  ASSERT_TRUE(streaming_decompressor.is_ok()) << streaming_decompressor.status_string();
  zx::result result = streaming_decompressor->Update(compressed_data());
  ASSERT_EQ(result.status_value(), kTestErrorCode) << result.status_string();
}

}  // namespace
}  // namespace blobfs
