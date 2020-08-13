// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/time.h>
#include <lib/inspect/cpp/inspect.h>
#include <unistd.h>
#include <zircon/time.h>

#include <array>
#include <thread>

#include <zxtest/zxtest.h>

#include "read-metrics.h"
#include "verification-metrics.h"

namespace blobfs {
namespace {

constexpr int kNumOperations = 5;
constexpr size_t kNumThreads = 5;
constexpr size_t MB = 1 << 20;
const zx_ticks_t ms = fzl::NsToTicks(zx::nsec(zx::msec(1).to_nsecs())).get();

TEST(ReadMetricsTest, UncompressedDiskRead) {
  inspect::Node metrics_node;
  ReadMetrics read_metrics(&metrics_node);

  auto stats = read_metrics.GetSnapshot(CompressionAlgorithm::UNCOMPRESSED);
  EXPECT_EQ(stats.read_bytes, 0);
  EXPECT_EQ(stats.read_ticks, 0);

  constexpr uint64_t kReadBytes = 1 * MB;
  const zx_ticks_t kReadDuration = 10 * ms;

  for (int i = 0; i < kNumOperations; i++) {
    read_metrics.IncrementDiskRead(CompressionAlgorithm::UNCOMPRESSED, kReadBytes,
                                   zx::ticks(kReadDuration));
  }

  stats = read_metrics.GetSnapshot(CompressionAlgorithm::UNCOMPRESSED);
  EXPECT_EQ(stats.read_bytes, kReadBytes * kNumOperations);
  EXPECT_EQ(stats.read_ticks, kReadDuration * kNumOperations);
}

TEST(ReadMetricsTest, ChunkedDecompression) {
  inspect::Node metrics_node;
  ReadMetrics read_metrics(&metrics_node);

  auto stats = read_metrics.GetSnapshot(CompressionAlgorithm::CHUNKED);
  EXPECT_EQ(stats.decompress_bytes, 0);
  EXPECT_EQ(stats.decompress_ticks, 0);

  constexpr uint64_t kDecompressBytes = 1 * MB;
  const zx_ticks_t kDecompressDuration = 10 * ms;

  for (int i = 0; i < kNumOperations; i++) {
    read_metrics.IncrementDecompression(CompressionAlgorithm::CHUNKED, kDecompressBytes,
                                        zx::ticks(kDecompressDuration));
  }

  stats = read_metrics.GetSnapshot(CompressionAlgorithm::CHUNKED);
  EXPECT_EQ(stats.decompress_bytes, kDecompressBytes * kNumOperations);
  EXPECT_EQ(stats.decompress_ticks, kDecompressDuration * kNumOperations);
}

TEST(VerificationMetricsTest, MerkleVerifyMultithreaded) {
  VerificationMetrics verification_metrics;

  auto stats = verification_metrics.Get();
  EXPECT_EQ(stats.blobs_verified, 0);
  EXPECT_EQ(stats.data_size, 0);
  EXPECT_EQ(stats.merkle_size, 0);
  EXPECT_EQ(stats.verification_time, 0);

  constexpr uint64_t kDataBytes = 10 * MB, kMerkleBytes = 1 * MB;
  const zx_ticks_t kDuration = 2 * ms;

  std::array<std::thread, kNumThreads> threads;
  for (auto &thread : threads) {
    thread = std::thread(
        [&]() { verification_metrics.Increment(kDataBytes, kMerkleBytes, zx::ticks(kDuration)); });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  stats = verification_metrics.Get();
  EXPECT_EQ(stats.blobs_verified, kNumThreads);
  EXPECT_EQ(stats.data_size, kDataBytes * kNumThreads);
  EXPECT_EQ(stats.merkle_size, kMerkleBytes * kNumThreads);
  EXPECT_EQ(stats.verification_time, kDuration * kNumThreads);
}

}  // namespace
}  // namespace blobfs
