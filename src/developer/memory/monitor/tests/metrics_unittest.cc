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
          },
  }});
  cobalt::FakeLogger_Sync logger;
  Metrics m(zx::msec(10), dispatcher(), &logger,
            [&cs](Capture& c, CaptureLevel l) { return cs.GetCapture(c, l); });
  RunLoopUntil([&cs] { return cs.empty(); });
  EXPECT_EQ(15U, logger.event_count());
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
            [&cs](Capture& c, CaptureLevel l) { return cs.GetCapture(c, l); });
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
            [&cs](Capture& c, CaptureLevel l) { return cs.GetCapture(c, l); });
  RunLoopUntil([&cs] { return cs.empty(); });
  EXPECT_EQ(2U, logger.event_count());
}

}  // namespace
}  // namespace monitor
