// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/digest.h"

#include <gtest/gtest.h>

#include "src/developer/memory/metrics/tests/test_utils.h"

namespace memory {
namespace test {

using DigestUnitTest = testing::Test;

TEST_F(DigestUnitTest, VMONames) {
  Capture c;
  TestUtils::CreateCapture(c, {
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

  Digest d(c, {{"A", ".*", "a.*"}, {"B", ".*", "b.*"}});
  auto const& buckets = d.buckets();
  ASSERT_EQ(2U, buckets.size());
  EXPECT_EQ(0U, d.undigested_vmos().size());
  auto b = buckets[0];
  EXPECT_STREQ("B", b.name().c_str());
  EXPECT_EQ(200U, b.size());
  b = buckets[1];
  EXPECT_STREQ("A", b.name().c_str());
  EXPECT_EQ(100U, b.size());
}  // namespace test

TEST_F(DigestUnitTest, ProcessNames) {
  Capture c;
  TestUtils::CreateCapture(c, {
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

  Digest d(c, {{"P", "p.*", ".*"}, {"Q", "q.*", ".*"}});
  auto const& buckets = d.buckets();
  ASSERT_EQ(2U, buckets.size());
  EXPECT_EQ(0U, d.undigested_vmos().size());
  auto b = buckets[0];
  EXPECT_STREQ("Q", b.name().c_str());
  EXPECT_EQ(200U, b.size());
  b = buckets[1];
  EXPECT_STREQ("P", b.name().c_str());
  EXPECT_EQ(100U, b.size());
}

TEST_F(DigestUnitTest, Undigested) {
  Capture c;
  TestUtils::CreateCapture(c, {
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

  Digest d(c, {{"A", ".*", "a.*"}});
  ASSERT_EQ(1U, d.undigested_vmos().size());
  ASSERT_NE(d.undigested_vmos().end(), d.undigested_vmos().find(2U));
  auto const& buckets = d.buckets();
  ASSERT_EQ(2U, buckets.size());
  auto b = buckets[0];
  EXPECT_STREQ("A", b.name().c_str());
  EXPECT_EQ(100U, b.size());
  b = buckets[1];
  EXPECT_STREQ("Undigested", b.name().c_str());
  EXPECT_EQ(200U, b.size());

}  // namespace test

TEST_F(DigestUnitTest, Kernel) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(c, {
                                  .kmem =
                                      {
                                          .total_bytes = 1000,
                                          .wired_bytes = 10,
                                          .total_heap_bytes = 20,
                                          .mmu_overhead_bytes = 30,
                                          .ipc_bytes = 40,
                                          .other_bytes = 50,
                                          .free_bytes = 100,
                                      },
                              });
  Digest d(c, {});
  auto const& buckets = d.buckets();
  EXPECT_EQ(0U, d.undigested_vmos().size());
  ASSERT_EQ(2U, buckets.size());
  auto b = buckets[0];
  EXPECT_STREQ("Kernel", b.name().c_str());
  EXPECT_EQ(150U, b.size());
  b = buckets[1];
  EXPECT_STREQ("Free", b.name().c_str());
  EXPECT_EQ(100U, b.size());
}

TEST_F(DigestUnitTest, Orphaned) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(c, {
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
  Digest d(c, {{"A", ".*", "a.*"}});
  auto const& buckets = d.buckets();
  EXPECT_EQ(0U, d.undigested_vmos().size());
  ASSERT_EQ(4U, buckets.size());
  auto b = buckets[0];
  EXPECT_STREQ("A", b.name().c_str());
  EXPECT_EQ(100U, b.size());
  b = buckets[1];
  EXPECT_STREQ("Orphaned", b.name().c_str());
  EXPECT_EQ(200U, b.size());
  b = buckets[2];
  EXPECT_STREQ("Kernel", b.name().c_str());
  EXPECT_EQ(0U, b.size());
  b = buckets[3];
  EXPECT_STREQ("Free", b.name().c_str());
  EXPECT_EQ(0U, b.size());
}

TEST_F(DigestUnitTest, DefaultBuckets) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(
      c, {
             .vmos =
                 {
                     {.koid = 1, .name = "", .committed_bytes = 1},
                     {.koid = 2, .name = "magma_create_buffer", .committed_bytes = 2},
                     {.koid = 3, .name = "Sysmem:buf", .committed_bytes = 3},
                     {.koid = 4, .name = "test", .committed_bytes = 4},
                     {.koid = 5, .name = "test", .committed_bytes = 5},
                     {.koid = 6, .name = "test", .committed_bytes = 6},
                     {.koid = 7, .name = "test", .committed_bytes = 7},
                     {.koid = 8, .name = "test", .committed_bytes = 8},
                     {.koid = 9, .name = "test", .committed_bytes = 9},
                     {.koid = 10, .name = "test", .committed_bytes = 10},
                     {.koid = 11, .name = "test", .committed_bytes = 11},
                     {.koid = 12, .name = "test", .committed_bytes = 12},
                     {.koid = 13, .name = "test", .committed_bytes = 13},
                     {.koid = 14, .name = "test", .committed_bytes = 14},
                     {.koid = 15, .name = "test", .committed_bytes = 15},
                     {.koid = 16, .name = "test", .committed_bytes = 16},
                 },
             .processes =
                 {
                     {.koid = 1, .name = "bin/bootsvc", .vmos = {1}},
                     {.koid = 2, .name = "test", .vmos = {2}},
                     {.koid = 3, .name = "devhost:sys", .vmos = {3}},
                     {.koid = 4, .name = "minfs:/data", .vmos = {4}},
                     {.koid = 5, .name = "blobfs:/blob", .vmos = {5}},
                     {.koid = 6, .name = "io.flutter.product_runner.jit", .vmos = {6}},
                     {.koid = 7, .name = "/pkg/web_engine_exe", .vmos = {7}},
                     {.koid = 8, .name = "kronk.cmx", .vmos = {8}},
                     {.koid = 9, .name = "scenic.cmx", .vmos = {9}},
                     {.koid = 10, .name = "devhost:pdev:05:00:f", .vmos = {10}},
                     {.koid = 11, .name = "netstack.cmx", .vmos = {11}},
                     {.koid = 12, .name = "amber.cmx", .vmos = {12}},
                     {.koid = 13, .name = "pkgfs", .vmos = {13}},
                     {.koid = 14, .name = "cast_agent.cmx", .vmos = {14}},
                     {.koid = 15, .name = "chromium.cmx", .vmos = {15}},
                     {.koid = 16, .name = "new", .vmos = {16}},
                 },
         });
  Digest d(c);
  auto const& buckets = d.buckets();
  EXPECT_EQ(1U, d.undigested_vmos().size());
  ASSERT_EQ(16U, buckets.size());
  for (uint64_t b = 0; b < 15; b++) {
    // They will be sorted in reverse order of size.
    uint64_t m = 15 - b;
    EXPECT_STREQ(Digest::kDefaultBucketMatches[m - 1].name.c_str(), buckets[b].name().c_str());
    EXPECT_EQ(m, buckets[b].size());
  }
  EXPECT_STREQ("Undigested", buckets[15].name().c_str());
  EXPECT_EQ(16U, buckets[15].size());
}

}  // namespace test
}  // namespace memory
