// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics.h"

#include <fuchsia/inspect/cpp/fidl.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/executor.h>
#include <lib/fzl/time.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/inspect/service/cpp/reader.h>
#include <lib/inspect/service/cpp/service.h>
#include <unistd.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <array>
#include <thread>

#include <gtest/gtest.h>

#include "blobfs/format.h"
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
  EXPECT_EQ(stats.read_bytes, 0u);
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
  EXPECT_EQ(stats.decompress_bytes, 0u);
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
  EXPECT_EQ(stats.blobs_verified, 0ul);
  EXPECT_EQ(stats.data_size, 0ul);
  EXPECT_EQ(stats.merkle_size, 0ul);
  EXPECT_EQ(stats.verification_time, 0);

  constexpr uint64_t kDataBytes = 10 * MB, kMerkleBytes = 1 * MB;
  const zx_ticks_t kDuration = 2 * ms;

  std::array<std::thread, kNumThreads> threads;
  for (auto& thread : threads) {
    thread = std::thread(
        [&]() { verification_metrics.Increment(kDataBytes, kMerkleBytes, zx::ticks(kDuration)); });
  }

  for (auto& thread : threads) {
    thread.join();
  }

  stats = verification_metrics.Get();
  EXPECT_EQ(stats.blobs_verified, kNumThreads);
  EXPECT_EQ(stats.data_size, kDataBytes * kNumThreads);
  EXPECT_EQ(stats.merkle_size, kMerkleBytes * kNumThreads);
  EXPECT_EQ(stats.verification_time, static_cast<zx_ticks_t>(kDuration * kNumThreads));
}

fit::result<inspect::Hierarchy> TakeSnapshot(fuchsia::inspect::TreePtr tree,
                                             async::Executor* executor) {
  std::condition_variable cv;
  std::mutex m;
  bool done = false;
  fit::result<inspect::Hierarchy> hierarchy_or_error;

  auto promise =
      inspect::ReadFromTree(std::move(tree)).then([&](fit::result<inspect::Hierarchy>& result) {
        {
          std::unique_lock<std::mutex> lock(m);
          hierarchy_or_error = std::move(result);
          done = true;
        }
        cv.notify_all();
      });

  executor->schedule_task(std::move(promise));

  std::unique_lock<std::mutex> lock(m);
  cv.wait(lock, [&done]() { return done; });

  return hierarchy_or_error;
}

TEST(MetricsTest, PageInMetrics) {
  // Setup an async thread on which the Inspect client and server can operate
  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("inspect-thread");
  async::Executor executor(loop.dispatcher());

  // Create the Metrics object (with page-in recording enabled) and record a page-in
  BlobfsMetrics metrics{true};
  metrics.IncrementPageIn("0123456789", 8192, 100);

  // Setup a connection to the Inspect VMO
  auto connector =
      inspect::MakeTreeHandler(metrics.inspector(), loop.dispatcher(),
                               inspect::TreeHandlerSettings{.force_private_snapshot = true});
  fuchsia::inspect::TreePtr tree;
  fidl::InterfaceRequest<fuchsia::inspect::Tree> request = tree.NewRequest(loop.dispatcher());
  connector(std::move(request));

  // Take a snapshot of the tree and verify the hierarchy
  fit::result<inspect::Hierarchy> result = TakeSnapshot(std::move(tree), &executor);
  EXPECT_TRUE(result.is_ok());

  inspect::Hierarchy hierarchy = result.take_value();
  const inspect::Hierarchy* blob_frequencies =
      hierarchy.GetByPath({"page_in_frequency_stats", "0123456789"});
  EXPECT_NE(blob_frequencies, nullptr);

  // Block index is counted in multiples of 8192
  const inspect::UintPropertyValue* frequency =
      blob_frequencies->node().get_property<inspect::UintPropertyValue>("1");
  EXPECT_NE(frequency, nullptr);
  EXPECT_EQ(frequency->value(), 1ul);

  loop.Quit();
  loop.JoinThreads();
}

}  // namespace
}  // namespace blobfs
