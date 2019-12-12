// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/metrics.h"

#include <lib/gtest/real_loop_fixture.h>
#include <zircon/time.h>

#include <gtest/gtest.h>
#include <src/cobalt/bin/testing/fake_logger.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/tests/test_utils.h"

using namespace memory;

using cobalt_registry::MemoryMetricDimensionBucket;
using fuchsia::cobalt::EventPayload;

namespace monitor {
namespace {

class MetricsUnitTest : public gtest::RealLoopFixture {};

TEST_F(MetricsUnitTest, All) {
  CaptureSupplier cs({{
      .time = zx_nsec_from_duration(zx_duration_from_hour(7)),
      .kmem =
          {
              .total_bytes = 2000,
              .free_bytes = 800,
              .wired_bytes = 60,
              .total_heap_bytes = 200,
              .free_heap_bytes = 80,
              .vmo_bytes = 900,
              .mmu_overhead_bytes = 60,
              .ipc_bytes = 10,
              .other_bytes = 20,
          },
      .vmos =
          {
              {.koid = 1, .name = "uncompressed-bootfs", .committed_bytes = 1},
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
              {.koid = 17, .name = "test", .committed_bytes = 17},
              {.koid = 18, .name = "test", .committed_bytes = 18},
          },
      .processes =
          {
              {.koid = 1, .name = "bin/bootsvc", .vmos = {1}},
              {.koid = 2, .name = "test", .vmos = {2}},
              {.koid = 3, .name = "devhost:sys", .vmos = {3}},
              {.koid = 4, .name = "/boot/bin/minfs", .vmos = {4}},
              {.koid = 5, .name = "/boot/bin/blobfs", .vmos = {5}},
              {.koid = 6, .name = "io.flutter.product_runner.aot", .vmos = {6}},
              {.koid = 7, .name = "/pkg/web_engine_exe:renderer", .vmos = {7}},
              {.koid = 8, .name = "kronk.cmx", .vmos = {8}},
              {.koid = 9, .name = "scenic.cmx", .vmos = {9}},
              {.koid = 10, .name = "devhost:pdev:05:00:f", .vmos = {10}},
              {.koid = 11, .name = "netstack.cmx", .vmos = {11}},
              {.koid = 12, .name = "amber.cmx", .vmos = {12}},
              {.koid = 13, .name = "pkgfs", .vmos = {13}},
              {.koid = 14, .name = "cast_agent.cmx", .vmos = {14}},
              {.koid = 15, .name = "chromium.cmx", .vmos = {15}},
              {.koid = 16, .name = "fshost", .vmos = {16}},
              {.koid = 17, .name = "archivist.cmx", .vmos = {17}},
              {.koid = 18, .name = "cobalt.cmx", .vmos = {18}},
          },
  }});
  cobalt::FakeLogger_Sync logger;
  Metrics m(zx::msec(10), dispatcher(), &logger, [&cs](Capture* c, CaptureLevel l) {
    return cs.GetCapture(c, l, true /*use_capture_supplier_time*/);
  });
  RunLoopUntil([&cs] { return cs.empty(); });
  // memory metric: 18 buckets + 3 (Orphaned, Kernel and Free buckets)  +
  // memory_general_breakdown metric: 10 +
  // memory_leak metric: 10
  // = 41
  EXPECT_EQ(41U, logger.logged_events().size());
  using Breakdown = cobalt_registry::MemoryGeneralBreakdownMetricDimensionGeneralBreakdown;
  using Breakdown2 = cobalt_registry::MemoryLeakMetricDimensionGeneralBreakdown;
  for (const auto& cobalt_event : logger.logged_events()) {
    EXPECT_EQ(EventPayload::Tag::kMemoryBytesUsed, cobalt_event.payload.Which());
    switch (cobalt_event.metric_id) {
      case cobalt_registry::kMemoryMetricId:
        ASSERT_EQ(1u, cobalt_event.event_codes.size());
        switch (cobalt_event.event_codes[0]) {
          case MemoryMetricDimensionBucket::Fshost:
            EXPECT_EQ(16u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Web:
            EXPECT_EQ(15u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Cast:
            EXPECT_EQ(14u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Orphaned:
            // 900 kmem.vmo - (1 + 2 + 3 + ... + 18) vmo digested in buckets = 764
            EXPECT_EQ(729u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Kernel:
            // 60 wired + 200 total_heap + 60 mmu_overhead + 10 ipc + 20 other = 350
            EXPECT_EQ(350u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Free:
            EXPECT_EQ(800u, cobalt_event.payload.memory_bytes_used());
            break;
          default:
            EXPECT_TRUE(cobalt_event.payload.memory_bytes_used() < 19);
            break;
        }
        break;
      case cobalt_registry::kMemoryGeneralBreakdownMetricId:
        ASSERT_EQ(1u, cobalt_event.event_codes.size());
        switch (cobalt_event.event_codes[0]) {
          case Breakdown::TotalBytes:
            EXPECT_EQ(2000u, cobalt_event.payload.memory_bytes_used());
            break;
          case Breakdown::UsedBytes:
            EXPECT_EQ(1200u, cobalt_event.payload.memory_bytes_used());
            break;
          case Breakdown::VmoBytes:
            EXPECT_EQ(900u, cobalt_event.payload.memory_bytes_used());
            break;
          case Breakdown::FreeBytes:
            EXPECT_EQ(800u, cobalt_event.payload.memory_bytes_used());
            break;
          default:
            EXPECT_TRUE(cobalt_event.payload.memory_bytes_used() <= 200);
            break;
        }
        break;
      case cobalt_registry::kMemoryLeakMetricId:
        ASSERT_EQ(2u, cobalt_event.event_codes.size());
        ASSERT_EQ(cobalt_registry::MemoryLeakMetricDimensionTimeSinceBoot::UpSixHours,
                  cobalt_event.event_codes[1]);
        switch (cobalt_event.event_codes[0]) {
          case Breakdown2::TotalBytes:
            EXPECT_EQ(2000u, cobalt_event.payload.memory_bytes_used());
            break;
          case Breakdown2::UsedBytes:
            EXPECT_EQ(1200u, cobalt_event.payload.memory_bytes_used());
            break;
          case Breakdown2::VmoBytes:
            EXPECT_EQ(900u, cobalt_event.payload.memory_bytes_used());
            break;
          case Breakdown2::FreeBytes:
            EXPECT_EQ(800u, cobalt_event.payload.memory_bytes_used());
            break;
          default:
            EXPECT_TRUE(cobalt_event.payload.memory_bytes_used() <= 200);
            break;
        }
        break;
    }
  }
}

TEST_F(MetricsUnitTest, One) {
  CaptureSupplier cs({{
      .kmem =
          {
              .total_bytes = 0,
              .free_bytes = 0,
              .wired_bytes = 0,
              .total_heap_bytes = 0,
              .free_heap_bytes = 0,
              .vmo_bytes = 0,
              .mmu_overhead_bytes = 0,
              .ipc_bytes = 0,
              .other_bytes = 0,
          },
      .vmos =
          {
              {.koid = 1, .name = "", .committed_bytes = 1},
          },
      .processes =
          {
              {.koid = 1, .name = "bin/bootsvc", .vmos = {1}},
          },
  }});
  cobalt::FakeLogger_Sync logger;
  Metrics m(zx::msec(10), dispatcher(), &logger,
            [&cs](Capture* c, CaptureLevel l) { return cs.GetCapture(c, l); });
  RunLoopUntil([&cs] { return cs.empty(); });
  EXPECT_EQ(21U, logger.event_count());  // 1 + 10 + 10
}

TEST_F(MetricsUnitTest, Undigested) {
  CaptureSupplier cs({{
      .kmem =
          {
              .total_bytes = 0,
              .free_bytes = 0,
              .wired_bytes = 0,
              .total_heap_bytes = 0,
              .free_heap_bytes = 0,
              .vmo_bytes = 0,
              .mmu_overhead_bytes = 0,
              .ipc_bytes = 0,
              .other_bytes = 0,
          },
      .vmos =
          {
              {.koid = 1, .name = "uncompressed-bootfs", .committed_bytes = 1},
              {.koid = 2, .name = "test", .committed_bytes = 2},
          },
      .processes =
          {
              {.koid = 1, .name = "bin/bootsvc", .vmos = {1}},
              {.koid = 2, .name = "test", .vmos = {2}},
          },
  }});
  cobalt::FakeLogger_Sync logger;
  Metrics m(zx::msec(10), dispatcher(), &logger,
            [&cs](Capture* c, CaptureLevel l) { return cs.GetCapture(c, l); });
  RunLoopUntil([&cs] { return cs.empty(); });
  EXPECT_EQ(22U, logger.event_count());  // 2 + 10 + 10
}

}  // namespace
}  // namespace monitor
