// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/summary.h"

#include <gtest/gtest.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/tests/test_utils.h"

namespace memory {
namespace test {

using SummaryUnitTest = testing::Test;

TEST_F(SummaryUnitTest, Single) {
  // One process, one vmo.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                       },
                                   .processes =
                                       {
                                           {.koid = 2, .name = "p1", .vmos = {1}},
                                       },
                               });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoVmos) {
  // One process, two vmos with same name.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                           {.koid = 2, .name = "v1", .committed_bytes = 100},
                                       },
                                   .processes =
                                       {
                                           {.koid = 2, .name = "p1", .vmos = {1, 2}},
                                       },
                               });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoVmoNames) {
  // One process, two vmos with different names.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                           {.koid = 2, .name = "v2", .committed_bytes = 100},
                                       },
                                   .processes = {{.koid = 2, .name = "p1", .vmos = {1, 2}}},
                               });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(2U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, Parent) {
  // One process, two vmos with different names, one is child.
  Capture c;
  TestUtils::CreateCapture(
      &c, {
              .vmos =
                  {
                      {.koid = 1, .name = "v1", .committed_bytes = 100},
                      {.koid = 2, .name = "v2", .committed_bytes = 100, .parent_koid = 1},
                  },
              .processes = {{.koid = 2, .name = "p1", .vmos = {2}}},
          });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(2U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoProcesses) {
  // Two processes, with different vmos.
  Capture c;
  TestUtils::CreateCapture(&c, {.vmos =
                                    {
                                        {.koid = 1, .name = "v1", .committed_bytes = 100},
                                        {.koid = 2, .name = "v2", .committed_bytes = 100},
                                    },
                                .processes = {
                                    {.koid = 2, .name = "p1", .vmos = {1}},
                                    {.koid = 3, .name = "p2", .vmos = {2}},
                                }});
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(3U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  ps = process_summaries.at(2);
  EXPECT_EQ(3U, ps.koid());
  EXPECT_STREQ("p2", ps.name().c_str());
  sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoProcessesShared) {
  // Two processes, with same vmos.
  Capture c;
  TestUtils::CreateCapture(&c, {.vmos =
                                    {
                                        {.koid = 1, .name = "v1", .committed_bytes = 100},
                                    },
                                .processes = {
                                    {.koid = 2, .name = "p1", .vmos = {1}},
                                    {.koid = 3, .name = "p2", .vmos = {1}},
                                }});
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(3U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  ps = process_summaries.at(2);
  EXPECT_EQ(3U, ps.koid());
  EXPECT_STREQ("p2", ps.name().c_str());
  sizes = ps.sizes();
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoProcessesChild) {
  // Two processes, with one vmo shared through parantage.
  Capture c;
  TestUtils::CreateCapture(
      &c, {.vmos =
               {
                   {.koid = 1, .name = "v1", .committed_bytes = 100},
                   {.koid = 2, .name = "v2", .committed_bytes = 100, .parent_koid = 1},
               },
           .processes = {
               {.koid = 2, .name = "p1", .vmos = {1}},
               {.koid = 3, .name = "p2", .vmos = {2}},
           }});
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(3U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  ps = process_summaries.at(2);
  EXPECT_EQ(3U, ps.koid());
  EXPECT_STREQ("p2", ps.name().c_str());
  sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(150U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(2U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, MissingParent) {
  // Child VMO with parent koid that's not found.
  Capture c;
  TestUtils::CreateCapture(
      &c, {.vmos =
               {
                   {.koid = 2, .name = "v2", .committed_bytes = 100, .parent_koid = 1},
               },
           .processes = {
               {.koid = 2, .name = "p1", .vmos = {2}},
           }});
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_STREQ("p1", ps.name().c_str());
  EXPECT_EQ(2U, ps.koid());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, Kernel) {
  // Test kernel stats.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .kmem =
                                       {
                                           .wired_bytes = 10,
                                           .total_heap_bytes = 20,
                                           .mmu_overhead_bytes = 30,
                                           .ipc_bytes = 40,
                                           .other_bytes = 50,
                                           .vmo_bytes = 60,
                                       },
                               });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(1U, process_summaries.size());
  ProcessSummary ps = process_summaries.at(0);
  EXPECT_EQ(ProcessSummary::kKernelKoid, ps.koid());
  EXPECT_STREQ("kernel", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(210U, sizes.private_bytes);
  EXPECT_EQ(210U, sizes.scaled_bytes);
  EXPECT_EQ(210U, sizes.total_bytes);

  EXPECT_EQ(6U, ps.name_to_sizes().size());

  sizes = ps.GetSizes("wired");
  EXPECT_EQ(10U, sizes.private_bytes);
  EXPECT_EQ(10U, sizes.scaled_bytes);
  EXPECT_EQ(10U, sizes.total_bytes);
  sizes = ps.GetSizes("heap");
  EXPECT_EQ(20U, sizes.private_bytes);
  EXPECT_EQ(20U, sizes.scaled_bytes);
  EXPECT_EQ(20U, sizes.total_bytes);
  sizes = ps.GetSizes("mmu");
  EXPECT_EQ(30U, sizes.private_bytes);
  EXPECT_EQ(30U, sizes.scaled_bytes);
  EXPECT_EQ(30U, sizes.total_bytes);
  sizes = ps.GetSizes("ipc");
  EXPECT_EQ(40U, sizes.private_bytes);
  EXPECT_EQ(40U, sizes.scaled_bytes);
  EXPECT_EQ(40U, sizes.total_bytes);
  sizes = ps.GetSizes("other");
  EXPECT_EQ(50U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(50U, sizes.total_bytes);
  sizes = ps.GetSizes("vmo");
  EXPECT_EQ(60U, sizes.private_bytes);
  EXPECT_EQ(60U, sizes.scaled_bytes);
  EXPECT_EQ(60U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, KernelVmo) {
  // Test kernel that kernel vmo memory that isn't found in
  // user space vmos is listed under the kernel.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .kmem =
                                       {
                                           .vmo_bytes = 110,
                                       },
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                       },
                                   .processes =
                                       {
                                           {.koid = 2, .name = "p1", .vmos = {1}},
                                       },
                               });
  Summary s(c);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());
  ProcessSummary ps = process_summaries.at(0);
  EXPECT_EQ(ProcessSummary::kKernelKoid, ps.koid());
  EXPECT_STREQ("kernel", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(10U, sizes.private_bytes);
  EXPECT_EQ(10U, sizes.scaled_bytes);
  EXPECT_EQ(10U, sizes.total_bytes);

  sizes = ps.GetSizes("vmo");
  EXPECT_EQ(10U, sizes.private_bytes);
  EXPECT_EQ(10U, sizes.scaled_bytes);
  EXPECT_EQ(10U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, NameMatch) {
  // One process, two vmos with same name.
  Capture c;
  TestUtils::CreateCapture(
      &c, {
              .vmos =
                  {
                      {.koid = 1, .name = "blob-12a", .committed_bytes = 100},
                      {.koid = 2, .name = "blob-merkle-12a", .committed_bytes = 100},
                      {.koid = 3, .name = "blob-de", .committed_bytes = 100},
                      {.koid = 4, .name = "pthread_t:0x59853000/TLS=0x548", .committed_bytes = 100},
                      {.koid = 5, .name = "thrd_t:0x59853000/TLS=0x548", .committed_bytes = 100},
                      {.koid = 6, .name = "data:libfoo.so", .committed_bytes = 100},
                      {.koid = 7, .name = "", .committed_bytes = 100},
                      {.koid = 8, .name = "scudo:primary", .committed_bytes = 100},
                      {.koid = 9, .name = "scudo:secondary", .committed_bytes = 100},
                      {.koid = 10, .name = "foo", .committed_bytes = 100},
                      {.koid = 11, .name = "initial-thread", .committed_bytes = 100},
                      {.koid = 12, .name = "libfoo.so.1", .committed_bytes = 100},
                  },
              .processes =
                  {
                      {.koid = 2, .name = "p1", .vmos = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}},
                  },
          });
  Summary s(c, Summary::kNameMatches);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(1200U, sizes.private_bytes);

  EXPECT_EQ(8U, ps.name_to_sizes().size());
  EXPECT_EQ(200U, ps.GetSizes("[blobs]").private_bytes);
  EXPECT_EQ(100U, ps.GetSizes("[blob-merkles]").private_bytes);
  EXPECT_EQ(300U, ps.GetSizes("[stacks]").private_bytes);
  EXPECT_EQ(100U, ps.GetSizes("[data]").private_bytes);
  EXPECT_EQ(100U, ps.GetSizes("[unnamed]").private_bytes);
  EXPECT_EQ(200U, ps.GetSizes("[scudo]").private_bytes);
  EXPECT_EQ(100U, ps.GetSizes("foo").private_bytes);
  EXPECT_EQ(100U, ps.GetSizes("[libraries]").private_bytes);
}

TEST_F(SummaryUnitTest, AllUndigested) {
  // One process, two vmos with different names.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                           {.koid = 2, .name = "v2", .committed_bytes = 100},
                                       },
                                   .processes = {{.koid = 2, .name = "p1", .vmos = {1, 2}}},
                               });
  Namer namer({});
  Summary s(c, &namer, {1, 2});
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(200U, sizes.private_bytes);
  EXPECT_EQ(200U, sizes.scaled_bytes);
  EXPECT_EQ(200U, sizes.total_bytes);

  EXPECT_EQ(2U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
  sizes = ps.GetSizes("v2");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, OneUndigested) {
  // One process, two vmos with different names.
  Capture c;
  TestUtils::CreateCapture(&c, {
                                   .vmos =
                                       {
                                           {.koid = 1, .name = "v1", .committed_bytes = 100},
                                           {.koid = 2, .name = "v2", .committed_bytes = 100},
                                       },
                                   .processes = {{.koid = 2, .name = "p1", .vmos = {1, 2}}},
                               });

  Namer namer({});
  Summary s(c, &namer, {1});
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, TwoProcessesOneUndigested) {
  // Two processes, with different vmos.
  Capture c;
  TestUtils::CreateCapture(&c, {.vmos =
                                    {
                                        {.koid = 1, .name = "v1", .committed_bytes = 100},
                                        {.koid = 2, .name = "v2", .committed_bytes = 100},
                                    },
                                .processes = {
                                    {.koid = 2, .name = "p1", .vmos = {1}},
                                    {.koid = 3, .name = "p2", .vmos = {2}},
                                }});
  Namer namer({});
  Summary s(c, &namer, {1});
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(2U, process_summaries.size());

  // Skip kernel summary.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(2U, ps.koid());
  EXPECT_STREQ("p1", ps.name().c_str());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  EXPECT_EQ(1U, ps.name_to_sizes().size());
  sizes = ps.GetSizes("v1");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);
}

TEST_F(SummaryUnitTest, Pools) {
  // One process, two vmos with same name.
  Capture c;
  TestUtils::CreateCapture(
      &c, {.vmos =
               {
                   {.koid = 1, .name = "SysmemContiguousPool", .committed_bytes = 400},
                   {.koid = 2, .name = "ContiguousChild", .size_bytes = 300, .parent_koid = 1},
                   {.koid = 3, .name = "ContiguousGrandchild", .size_bytes = 100, .parent_koid = 2},
                   {.koid = 4, .name = "ContiguousGrandchild", .size_bytes = 50, .parent_koid = 2},
                   {.koid = 5, .name = "Sysmem-core", .committed_bytes = 50},
                   {.koid = 6, .name = "CoreChild", .size_bytes = 50, .parent_koid = 5},
               },
           .processes =
               {
                   {.koid = 10, .name = "p1", .vmos = {1, 2, 5}},
                   {.koid = 20, .name = "p2", .vmos = {3}},
                   {.koid = 30, .name = "p3", .vmos = {4}},
                   {.koid = 40, .name = "p4", .vmos = {6}},
               },
           .rooted_vmo_names = Capture::kDefaultRootedVmoNames});
  Summary s(c, Summary::kNameMatches);
  auto process_summaries = TestUtils::GetProcessSummaries(s);
  ASSERT_EQ(5U, process_summaries.size());

  // Skip kernel summary.
  // SysmemContiguousPool will be left with 100 bytes, shared by all three processes.
  // ContiguousChild will be left with 150 bytes, shared by all three processes.
  // p2 will have a private ContiguousGrandchild VMO of 100 bytes.
  // p3 will have a private ContiguousGrandchild VMO of 50 bytes.
  ProcessSummary ps = process_summaries.at(1);
  EXPECT_EQ(10U, ps.koid());
  Sizes sizes = ps.sizes();
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(250U / 3, sizes.scaled_bytes);
  EXPECT_EQ(250U, sizes.total_bytes);

  sizes = ps.GetSizes("SysmemContiguousPool");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(100U / 3, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  sizes = ps.GetSizes("ContiguousChild");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(150U / 3, sizes.scaled_bytes);
  EXPECT_EQ(150U, sizes.total_bytes);

  ps = process_summaries.at(2);
  EXPECT_EQ(20U, ps.koid());
  sizes = ps.sizes();
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U + 250U / 3, sizes.scaled_bytes);
  EXPECT_EQ(100U + 250U, sizes.total_bytes);

  sizes = ps.GetSizes("SysmemContiguousPool");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(100U / 3, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  sizes = ps.GetSizes("ContiguousChild");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(150U / 3, sizes.scaled_bytes);
  EXPECT_EQ(150U, sizes.total_bytes);

  sizes = ps.GetSizes("ContiguousGrandchild");
  EXPECT_EQ(100U, sizes.private_bytes);
  EXPECT_EQ(100U, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  ps = process_summaries.at(3);
  EXPECT_EQ(30U, ps.koid());
  sizes = ps.sizes();
  EXPECT_EQ(50U, sizes.private_bytes);
  EXPECT_EQ(50U + 250U / 3, sizes.scaled_bytes);
  EXPECT_EQ(50U + 250U, sizes.total_bytes);

  sizes = ps.GetSizes("SysmemContiguousPool");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(100U / 3, sizes.scaled_bytes);
  EXPECT_EQ(100U, sizes.total_bytes);

  sizes = ps.GetSizes("ContiguousChild");
  EXPECT_EQ(0U, sizes.private_bytes);
  EXPECT_EQ(150U / 3, sizes.scaled_bytes);
  EXPECT_EQ(150U, sizes.total_bytes);

  sizes = ps.GetSizes("ContiguousGrandchild");
  EXPECT_EQ(50U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(50U, sizes.total_bytes);

  ps = process_summaries.at(4);
  EXPECT_EQ(40U, ps.koid());
  sizes = ps.sizes();
  EXPECT_EQ(50U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(50U, sizes.total_bytes);

  sizes = ps.GetSizes("CoreChild");
  EXPECT_EQ(50U, sizes.private_bytes);
  EXPECT_EQ(50U, sizes.scaled_bytes);
  EXPECT_EQ(50U, sizes.total_bytes);
}

}  // namespace test
}  // namespace memory
