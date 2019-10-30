// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/metrics.h"

#include <lib/gtest/real_loop_fixture.h>

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
              {.koid = 4, .name = "/boot/bin/minfs", .vmos = {4}},
              {.koid = 5, .name = "/boot/bin/blobfs", .vmos = {5}},
              {.koid = 6, .name = "io.flutter.product_runner.aot", .vmos = {6}},
              {.koid = 7, .name = "/pkg/web_engine_exe", .vmos = {7}},
              {.koid = 8, .name = "kronk.cmx", .vmos = {8}},
              {.koid = 9, .name = "scenic.cmx", .vmos = {9}},
              {.koid = 10, .name = "devhost:pdev:05:00:f", .vmos = {10}},
              {.koid = 11, .name = "netstack.cmx", .vmos = {11}},
              {.koid = 12, .name = "amber.cmx", .vmos = {12}},
              {.koid = 13, .name = "pkgfs", .vmos = {13}},
              {.koid = 14, .name = "cast_agent.cmx", .vmos = {14}},
              {.koid = 15, .name = "chromium.cmx", .vmos = {15}},
              {.koid = 16, .name = "fshost", .vmos = {16}},
          },
  }});
  cobalt::FakeLogger_Sync logger;
  Metrics m(zx::msec(10), dispatcher(), &logger,
            [&cs](Capture* c, CaptureLevel l) { return cs.GetCapture(c, l); });
  RunLoopUntil([&cs] { return cs.empty(); });
  EXPECT_EQ(16U, logger.logged_events().size());
  for (const auto& cobalt_event : logger.logged_events()) {
    EXPECT_EQ(1u, cobalt_event.metric_id);
    ASSERT_EQ(1u, cobalt_event.event_codes.size());
    EXPECT_EQ(EventPayload::Tag::kMemoryBytesUsed, cobalt_event.payload.Which());
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

      default:
        EXPECT_TRUE(cobalt_event.payload.memory_bytes_used() < 14);
        break;
    }
  }
}

TEST_F(MetricsUnitTest, One) {
  CaptureSupplier cs({{
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
  EXPECT_EQ(1U, logger.event_count());
}

TEST_F(MetricsUnitTest, Undigested) {
  CaptureSupplier cs({{
      .vmos =
          {
              {.koid = 1, .name = "", .committed_bytes = 1},
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
  EXPECT_EQ(2U, logger.event_count());
}

}  // namespace
}  // namespace monitor
