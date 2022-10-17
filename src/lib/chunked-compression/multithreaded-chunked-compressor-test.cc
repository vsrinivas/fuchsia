// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/chunked-compression/multithreaded-chunked-compressor.h"

#include <lib/stdcompat/span.h>
#include <zircon/errors.h>

#include <thread>
#include <vector>

#include <fbl/array.h>
#include <zxtest/zxtest.h>

#include "src/lib/chunked-compression/chunked-archive.h"
#include "src/lib/chunked-compression/chunked-decompressor.h"
#include "src/lib/chunked-compression/compression-params.h"

namespace chunked_compression {
namespace {

constexpr size_t kThreadCount = 2;
constexpr size_t kChunkSize = 8192;

std::vector<uint8_t> CreateRandomData(size_t size) {
  std::vector<uint8_t> data(size);
  unsigned int seed = zxtest::Runner::GetInstance()->random_seed();
  size_t off = 0;
  size_t rounded_len = fbl::round_down(size, sizeof(int));
  for (off = 0; off < rounded_len; off += sizeof(int)) {
    *reinterpret_cast<int*>(data.data() + off) = rand_r(&seed);
  }
  ZX_ASSERT(off == rounded_len);
  for (; off < size; ++off) {
    data[off] = static_cast<uint8_t>(rand_r(&seed));
  }
  return data;
}

void CheckCompressedData(const std::vector<uint8_t>& compressed_data,
                         const std::vector<uint8_t>& data) {
  fbl::Array<uint8_t> decompressed_data;
  size_t decompressed_size;
  ASSERT_OK(ChunkedDecompressor::DecompressBytes(compressed_data.data(), compressed_data.size(),
                                                 &decompressed_data, &decompressed_size));
  ASSERT_EQ(decompressed_data.size(), data.size());
  ASSERT_BYTES_EQ(decompressed_data.data(), data.data(), data.size());
}

TEST(MultithreadedChunkedCompressorTest, EmptyInput) {
  CompressionParams params;
  MultithreadedChunkedCompressor compressor(kThreadCount);
  auto result = compressor.Compress(params, cpp20::span<const uint8_t>());
  ASSERT_OK(result.status_value());
  ASSERT_TRUE(result->empty());
}

TEST(MultithreadedChunkedCompressorTest, ChunkAlignedInput) {
  CompressionParams params{
      .chunk_size = kChunkSize,
  };
  auto data = CreateRandomData(kChunkSize * 2);
  MultithreadedChunkedCompressor compressor(kThreadCount);
  auto result = compressor.Compress(params, data);
  ASSERT_OK(result.status_value());
  ASSERT_NO_FATAL_FAILURE(CheckCompressedData(result.value(), data));
}

TEST(MultithreadedChunkedCompressorTest, ChunkUnalignedInput) {
  CompressionParams params{
      .chunk_size = kChunkSize,
  };
  auto data = CreateRandomData(kChunkSize * 2 + 200);
  MultithreadedChunkedCompressor compressor(kThreadCount);
  auto result = compressor.Compress(params, data);
  ASSERT_OK(result.status_value());
  ASSERT_NO_FATAL_FAILURE(CheckCompressedData(result.value(), data));
}

TEST(MultithreadedChunkedCompressorTest, InputTooLargeForChunkSize) {
  CompressionParams params{
      .chunk_size = kChunkSize,
  };
  std::vector<uint8_t> data(kChunkSize * (kChunkArchiveMaxFrames + 1));
  MultithreadedChunkedCompressor compressor(kThreadCount);
  auto result = compressor.Compress(params, data);
  ASSERT_EQ(result.status_value(), ZX_ERR_INVALID_ARGS);
}

TEST(MultithreadedChunkedCompressorTest, CompressMultipleBuffersWithDifferentParamsAtOnce) {
  struct CompressionTask {
    std::vector<uint8_t> data;
    zx::result<std::vector<uint8_t>> result;
    CompressionParams params;
  };
  std::vector<CompressionTask> compression_tasks = {
      CompressionTask{
          .data = CreateRandomData(kChunkSize * 2 + 5),
          .params = CompressionParams{.chunk_size = kChunkSize},
      },
      CompressionTask{
          .data = CreateRandomData(kChunkSize * 2),
          .params =
              CompressionParams{
                  .chunk_size = kChunkSize,
                  .frame_checksum = true,
              },

      },
      CompressionTask{
          .data = CreateRandomData(kChunkSize * 4),
          .params = CompressionParams{.chunk_size = kChunkSize * 2},
      },
  };
  std::vector<std::thread> threads;
  MultithreadedChunkedCompressor compressor(kThreadCount);
  for (auto& task : compression_tasks) {
    threads.emplace_back(
        [&task, &compressor]() { task.result = compressor.Compress(task.params, task.data); });
  }
  for (auto& thread : threads) {
    thread.join();
  }
  for (const auto& task : compression_tasks) {
    ASSERT_OK(task.result.status_value());
    ASSERT_NO_FATAL_FAILURE(CheckCompressedData(task.result.value(), task.data));
    SeekTable seek_table;
    HeaderReader header_read;
    ASSERT_OK(header_read.Parse(task.result->data(), task.result->size(), task.result->size(),
                                &seek_table));
    // All of the tasks have 2 chunks so the decompressed size of the first chunk should be the
    // chunk size.
    ASSERT_GE(seek_table.Entries().size(), 2);
    ASSERT_EQ(seek_table.Entries()[0].decompressed_size, task.params.chunk_size);
  }
}

}  // namespace
}  // namespace chunked_compression
