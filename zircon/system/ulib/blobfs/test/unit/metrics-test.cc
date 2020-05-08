// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/time.h>
#include <unistd.h>
#include <zircon/time.h>

#include <array>
#include <thread>

#include <zxtest/zxtest.h>

#include "read-metrics.h"
#include "verification-metrics.h"

namespace blobfs {
namespace {

constexpr size_t kNumThreads = 5;
constexpr size_t MB = 1 << 20;
const zx_ticks_t ms = fzl::NsToTicks(zx::nsec(zx::msec(1).to_nsecs())).get();

TEST(MetricsTest, MerkleDiskReadMultithreaded) {
  ReadMetrics read_metrics;

  auto stats = read_metrics.GetDiskRead();
  EXPECT_EQ(stats.read_size, 0);
  EXPECT_EQ(stats.read_time, 0);

  constexpr uint64_t kReadBytes = 1 * MB;
  const zx_ticks_t kReadDuration = 10 * ms;

  std::array<std::thread, kNumThreads> threads;
  for (auto &thread : threads) {
    thread = std::thread(
        [&]() { read_metrics.IncrementDiskRead(kReadBytes, zx::ticks(kReadDuration)); });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  stats = read_metrics.GetDiskRead();
  EXPECT_EQ(stats.read_size, kReadBytes * kNumThreads);
  EXPECT_EQ(stats.read_time, kReadDuration * kNumThreads);
}

TEST(MetricsTest, MerkleDecompressMultithreaded) {
  ReadMetrics read_metrics;

  auto stats = read_metrics.GetDecompression();
  EXPECT_EQ(stats.compr_size, 0);
  EXPECT_EQ(stats.decompr_size, 0);
  EXPECT_EQ(stats.compr_read_time, 0);
  EXPECT_EQ(stats.decompr_time, 0);

  constexpr uint64_t kComprBytes = 1 * MB, kUncomprBytes = 2 * MB;
  const zx_ticks_t kReadDuration = 20 * ms, kDecomprDuration = 10 * ms;

  std::array<std::thread, kNumThreads> threads;
  for (auto &thread : threads) {
    thread = std::thread([&]() {
      read_metrics.IncrementDecompression(kComprBytes, kUncomprBytes, zx::ticks(kReadDuration),
                                          zx::ticks(kDecomprDuration));
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  stats = read_metrics.GetDecompression();
  EXPECT_EQ(stats.compr_size, kComprBytes * kNumThreads);
  EXPECT_EQ(stats.decompr_size, kUncomprBytes * kNumThreads);
  EXPECT_EQ(stats.compr_read_time, kReadDuration * kNumThreads);
  EXPECT_EQ(stats.decompr_time, kDecomprDuration * kNumThreads);
}

TEST(MetricsTest, MerkleVerifyMultithreaded) {
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
