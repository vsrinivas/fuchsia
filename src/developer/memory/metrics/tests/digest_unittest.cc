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

}  // namespace test
}  // namespace memory
