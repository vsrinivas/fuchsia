// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/digest.h"

#include <gtest/gtest.h>

#include "src/developer/memory/metrics/tests/test_utils.h"

namespace memory {
namespace test {

using DigestUnitTest = testing::Test;

struct ExpectedBucket {
  std::string name;
  uint64_t size;
};

void ConfirmBuckets(const Digest& digest, const std::vector<ExpectedBucket>& expected_buckets) {
  std::vector<Bucket> buckets_copy = digest.buckets();
  for (size_t i = 0; i < expected_buckets.size(); ++i) {
    const auto& expected_bucket = expected_buckets.at(i);
    bool found = false;
    for (size_t j = 0; j < buckets_copy.size(); ++j) {
      const auto& bucket = buckets_copy.at(j);

      if (expected_bucket.name == bucket.name()) {
        EXPECT_EQ(expected_bucket.size, bucket.size()) << "Bucket name: " << expected_bucket.name;
        buckets_copy.erase(buckets_copy.begin() + j);
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "Bucket name: " << expected_bucket.name;
  }
  for (const auto& unmatched_bucket : buckets_copy) {
    EXPECT_TRUE(false) << "Unmatched bucket: " << unmatched_bucket.name();
  }
}

TEST_F(DigestUnitTest, VMONames) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "a1", .committed_bytes = 100},
                                           {.koid = 2, .name = "b1", .committed_bytes = 200},
                                       },
                                   .processes =
                                       {
                                           {.koid = 1, .name = "p1", .vmos = {1}},
                                           {.koid = 2, .name = "q1", .vmos = {2}},
                                       },
                               });

  Digester digester({{"A", "", "a.*"}, {"B", ".*", "b.*"}});
  Digest d(c, &digester);
  ConfirmBuckets(d, {{"B", 200U}, {"A", 100U}});
  EXPECT_EQ(0U, d.undigested_vmos().size());
}  // namespace test

TEST_F(DigestUnitTest, ProcessNames) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "a1", .committed_bytes = 100},
                                           {.koid = 2, .name = "b1", .committed_bytes = 200},
                                       },
                                   .processes =
                                       {
                                           {.koid = 1, .name = "p1", .vmos = {1}},
                                           {.koid = 2, .name = "q1", .vmos = {2}},
                                       },
                               });

  Digester digester({{"P", "p.*", ""}, {"Q", "q.*", ".*"}});
  Digest d(c, &digester);
  ConfirmBuckets(d, {{"Q", 200U}, {"P", 100U}});
  EXPECT_EQ(0U, d.undigested_vmos().size());
}

TEST_F(DigestUnitTest, Undigested) {
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "a1", .committed_bytes = 100},
                                           {.koid = 2, .name = "b1", .committed_bytes = 200},
                                       },
                                   .processes =
                                       {
                                           {.koid = 1, .name = "p1", .vmos = {1}},
                                           {.koid = 2, .name = "q1", .vmos = {2}},
                                       },
                               });

  Digester digester({{"A", ".*", "a.*"}});
  Digest d(c, &digester);
  ASSERT_EQ(1U, d.undigested_vmos().size());
  ASSERT_NE(d.undigested_vmos().end(), d.undigested_vmos().find(2U));
  ConfirmBuckets(d, {{"A", 100U}, {"Undigested", 200U}});
}  // namespace test

TEST_F(DigestUnitTest, Kernel) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .kmem =
                                       {
                                           .total_bytes = 1000,
                                           .free_bytes = 100,
                                           .wired_bytes = 10,
                                           .total_heap_bytes = 20,
                                           .mmu_overhead_bytes = 30,
                                           .ipc_bytes = 40,
                                           .other_bytes = 50,
                                       },
                               });
  Digester digester({});
  Digest d(c, &digester);
  EXPECT_EQ(0U, d.undigested_vmos().size());
  ConfirmBuckets(d, {{"Kernel", 150U}, {"Free", 100U}});
}

TEST_F(DigestUnitTest, Orphaned) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .kmem =
                                       {
                                           .total_bytes = 1000,
                                           .vmo_bytes = 300,
                                       },
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "a1", .committed_bytes = 100},
                                       },
                                   .processes =
                                       {
                                           {.koid = 1, .name = "p1", .vmos = {1}},
                                       },
                               });
  Digester digester({{"A", ".*", "a.*"}});
  Digest d(c, &digester);
  EXPECT_EQ(0U, d.undigested_vmos().size());
  ConfirmBuckets(d, {{"A", 100U}, {"Orphaned", 200U}, {"Kernel", 0U}, {"Free", 0U}});
}

TEST_F(DigestUnitTest, DefaultBuckets) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(
      &c, {.vmos =
               {
                   {.koid = 1, .name = "uncompressed-bootfs", .committed_bytes = 1},
                   {.koid = 2, .name = "magma_create_buffer", .committed_bytes = 2},
                   {.koid = 3, .name = "SysmemAmlogicProtectedPool", .committed_bytes = 3},
                   {.koid = 4, .name = "SysmemContiguousPool", .committed_bytes = 4},
                   {.koid = 5, .name = "test", .committed_bytes = 5},
                   {.koid = 6, .name = "test", .committed_bytes = 6},
                   {.koid = 7, .name = "test", .committed_bytes = 7},
                   {.koid = 8, .name = "dart", .committed_bytes = 8},
                   {.koid = 9, .name = "test", .committed_bytes = 9},
                   {.koid = 10, .name = "test", .committed_bytes = 10},
                   {.koid = 11, .name = "test", .committed_bytes = 11},
                   {.koid = 12, .name = "test", .committed_bytes = 12},
                   {.koid = 13, .name = "test", .committed_bytes = 13},
                   {.koid = 14, .name = "test", .committed_bytes = 14},
                   {.koid = 15, .name = "test", .committed_bytes = 15},
                   {.koid = 16, .name = "test", .committed_bytes = 16},
                   {.koid = 17, .name = "test", .committed_bytes = 17},
                   {.koid = 18, .name = "test", .committed_bytes = 18},
                   {.koid = 19, .name = "test", .committed_bytes = 19},
                   {.koid = 20, .name = "test", .committed_bytes = 20},
                   {.koid = 21, .name = "test", .committed_bytes = 21},
                   {.koid = 22, .name = "test", .committed_bytes = 22},
                   {.koid = 23, .name = "inactive-blob-123", .committed_bytes = 23},
                   {.koid = 24, .name = "blob-abc", .committed_bytes = 24},
                   {.koid = 25, .name = "Mali JIT memory", .committed_bytes = 25},
                   {.koid = 26, .name = "MagmaProtectedSysmem", .committed_bytes = 26},
                   {.koid = 27, .name = "ImagePipe2Surface:0", .committed_bytes = 27},
                   {.koid = 28, .name = "GFXBufferCollection:1", .committed_bytes = 28},
                   {.koid = 29, .name = "ScenicImageMemory", .committed_bytes = 29},
                   {.koid = 30, .name = "Display:0", .committed_bytes = 30},
                   {.koid = 31, .name = "Display-Protected:0", .committed_bytes = 31},
                   {.koid = 32, .name = "CompactImage:0", .committed_bytes = 32},
                   {.koid = 33, .name = "GFX Device Memory CPU Uncached", .committed_bytes = 33},
               },
           .processes = {
               {.koid = 1, .name = "bin/bootsvc", .vmos = {1}},
               {.koid = 2, .name = "test", .vmos = {2, 25, 26}},
               {.koid = 3, .name = "driver_host:pdev:00:00:1b", .vmos = {3, 4}},
               {.koid = 4, .name = "fshost.cm", .vmos = {5}},
               {.koid = 5, .name = "/boot/bin/minfs", .vmos = {6}},
               {.koid = 6, .name = "/boot/bin/blobfs", .vmos = {7, 23, 24}},
               {.koid = 7, .name = "io.flutter.product_runner.aot", .vmos = {8, 9, 28, 29}},
               {.koid = 10, .name = "kronk.cmx", .vmos = {10}},
               {.koid = 8, .name = "web_engine_exe:renderer", .vmos = {11}},
               {.koid = 9, .name = "web_engine_exe:gpu", .vmos = {12, 27, 32, 33}},
               {.koid = 11, .name = "scenic.cmx", .vmos = {13, 27, 28, 29, 30, 31}},
               {.koid = 12, .name = "driver_host:pdev:05:00:f", .vmos = {14}},
               {.koid = 13, .name = "netstack.cmx", .vmos = {15}},
               {.koid = 14, .name = "pkgfs", .vmos = {16}},
               {.koid = 15, .name = "cast_agent.cmx", .vmos = {17}},
               {.koid = 16, .name = "archivist.cm", .vmos = {18}},
               {.koid = 17, .name = "cobalt.cmx", .vmos = {19}},
               {.koid = 18, .name = "audio_core.cmx", .vmos = {20}},
               {.koid = 19, .name = "context_provider.cmx", .vmos = {21}},
               {.koid = 20, .name = "new", .vmos = {22}},
           }});

  const std::vector<BucketMatch> bucket_matches = {
      {"ZBI Buffer", ".*", "uncompressed-bootfs"},
      // Memory used with the GPU or display hardware.
      {"Graphics", ".*",
       "magma_create_buffer|Mali "
       ".*|Magma.*|ImagePipe2Surface.*|GFXBufferCollection.*|ScenicImageMemory|Display.*|"
       "CompactImage.*|GFX Device Memory.*"},
      // Unused protected pool memory.
      {"ProtectedPool", "driver_host:.*", "SysmemAmlogicProtectedPool"},
      // Unused contiguous pool memory.
      {"ContiguousPool", "driver_host:.*", "SysmemContiguousPool"},
      {"Fshost", "fshost.cm", ".*"},
      {"Minfs", ".*minfs", ".*"},
      {"BlobfsInactive", ".*blobfs", "inactive-blob-.*"},
      {"Blobfs", ".*blobfs", ".*"},
      {"FlutterApps", "io\\.flutter\\..*", "dart.*"},
      {"Flutter", "io\\.flutter\\..*", ".*"},
      {"Web", "web_engine_exe:.*", ".*"},
      {"Kronk", "kronk.cmx", ".*"},
      {"Scenic", "scenic.cmx", ".*"},
      {"Amlogic", "driver_host:pdev:05:00:f", ".*"},
      {"Netstack", "netstack.cmx", ".*"},
      {"Pkgfs", "pkgfs", ".*"},
      {"Cast", "cast_agent.cmx", ".*"},
      {"Archivist", "archivist.cm", ".*"},
      {"Cobalt", "cobalt.cmx", ".*"},
      {"Audio", "audio_core.cmx", ".*"},
      {"Context", "context_provider.cmx", ".*"},
  };

  Digester digester(bucket_matches);
  Digest d(c, &digester);
  EXPECT_EQ(1U, d.undigested_vmos().size());

  ConfirmBuckets(d, {
                        {"Web", 23U},
                        {"Context", 21U},
                        {"Audio", 20U},
                        {"Cobalt", 19U},
                        {"Archivist", 18U},
                        {"Cast", 17U},
                        {"Pkgfs", 16U},
                        {"Netstack", 15U},
                        {"Amlogic", 14U},
                        {"Scenic", 13U},
                        {"Kronk", 10U},
                        {"Flutter", 9U},
                        {"FlutterApps", 8U},
                        {"Blobfs", 31U},
                        {"Minfs", 6U},
                        {"Fshost", 5U},
                        {"ContiguousPool", 4U},
                        {"ProtectedPool", 3U},
                        {"Graphics", 2U + 25U + 26U + 27U + 28U + 29U + 30U + 31U + 32U + 33U},
                        {"ZBI Buffer", 1U},
                        {"BlobfsInactive", 23U},
                        {"Undigested", 22U},
                    });
}

}  // namespace test
}  // namespace memory
