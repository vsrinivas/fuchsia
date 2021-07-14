// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/metrics.h"

#include <lib/async/cpp/executor.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
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
const std::vector<BucketMatch> kBucketMatches = {
    {"ZBI Buffer", ".*", "uncompressed-bootfs", MemoryMetricDimensionBucket::ZbiBuffer},
    // Memory used with the GPU or display hardware.
    {"Graphics", ".*",
     "magma_create_buffer|Mali "
     ".*|Magma.*|ImagePipe2Surface.*|GFXBufferCollection.*|ScenicImageMemory|Display.*|"
     "CompactImage.*|GFX Device Memory.*",
     MemoryMetricDimensionBucket::Graphics},
    // Unused protected pool memory.
    {"ProtectedPool", "driver_host:.*", "SysmemAmlogicProtectedPool",
     MemoryMetricDimensionBucket::ProtectedPool},
    // Unused contiguous pool memory.
    {"ContiguousPool", "driver_host:.*", "SysmemContiguousPool",
     MemoryMetricDimensionBucket::ContiguousPool},
    {"Fshost", "fshost.cm", ".*", MemoryMetricDimensionBucket::Fshost},
    {"Minfs", ".*minfs", ".*", MemoryMetricDimensionBucket::Minfs},
    {"BlobfsInactive", ".*blobfs", "inactive-blob-.*", MemoryMetricDimensionBucket::BlobfsInactive},
    {"Blobfs", ".*blobfs", ".*", MemoryMetricDimensionBucket::Blobfs},
    {"FlutterApps", "io\\.flutter\\..*", "dart.*", MemoryMetricDimensionBucket::FlutterApps},
    {"Flutter", "io\\.flutter\\..*", ".*", MemoryMetricDimensionBucket::Flutter},
    {"Web", "web_engine_exe:.*", ".*", MemoryMetricDimensionBucket::Web},
    {"Kronk", "kronk.cmx|kronk_for_testing.cmx", ".*", MemoryMetricDimensionBucket::Kronk},
    {"Scenic", "scenic.cmx", ".*", MemoryMetricDimensionBucket::Scenic},
    {"Amlogic", "driver_host:pdev:05:00:f", ".*", MemoryMetricDimensionBucket::Amlogic},
    {"Netstack", "netstack.cmx", ".*", MemoryMetricDimensionBucket::Netstack},
    {"Pkgfs", "pkgfs", ".*", MemoryMetricDimensionBucket::Pkgfs},
    {"Cast", "cast_agent.cmx", ".*", MemoryMetricDimensionBucket::Cast},
    {"Archivist", "archivist.cm", ".*", MemoryMetricDimensionBucket::Archivist},
    {"Cobalt", "cobalt.cmx", ".*", MemoryMetricDimensionBucket::Cobalt},
    {"Audio", "audio_core.cmx", ".*", MemoryMetricDimensionBucket::Audio},
    {"Context", "context_provider.cmx", ".*", MemoryMetricDimensionBucket::Context},
};

class MetricsUnitTest : public gtest::RealLoopFixture {
 public:
  MetricsUnitTest() : executor_(dispatcher()) {}

 protected:
  // Run a promise to completion on the default async executor.
  void RunPromiseToCompletion(fpromise::promise<> promise) {
    bool done = false;
    executor_.schedule_task(std::move(promise).and_then([&]() { done = true; }));
    RunLoopUntilIdle();
    ASSERT_TRUE(done);
  }
  std::vector<CaptureTemplate> Template() {
    return std::vector<CaptureTemplate>{{
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
            },
        .processes =
            {
                {.koid = 1, .name = "bin/bootsvc", .vmos = {1}},
                {.koid = 2, .name = "test", .vmos = {2}},
                {.koid = 3, .name = "driver_host:sys", .vmos = {3, 4}},
                {.koid = 4, .name = "fshost.cm", .vmos = {5}},
                {.koid = 5, .name = "/boot/bin/minfs", .vmos = {6}},
                {.koid = 6, .name = "/boot/bin/blobfs", .vmos = {7}},
                {.koid = 7, .name = "io.flutter.product_runner.aot", .vmos = {8, 9}},
                {.koid = 8, .name = "web_engine_exe:renderer", .vmos = {10}},
                {.koid = 9, .name = "web_engine_exe:gpu", .vmos = {11}},
                {.koid = 10, .name = "kronk.cmx", .vmos = {12}},
                {.koid = 11, .name = "scenic.cmx", .vmos = {13}},
                {.koid = 12, .name = "driver_host:pdev:05:00:f", .vmos = {14}},
                {.koid = 13, .name = "netstack.cmx", .vmos = {15}},
                {.koid = 14, .name = "pkgfs", .vmos = {16}},
                {.koid = 15, .name = "cast_agent.cmx", .vmos = {17}},
                {.koid = 16, .name = "archivist.cm", .vmos = {18}},
                {.koid = 17, .name = "cobalt.cmx", .vmos = {19}},
                {.koid = 18, .name = "audio_core.cmx", .vmos = {20}},
                {.koid = 19, .name = "context_provider.cmx", .vmos = {21}},
                {.koid = 20, .name = "new", .vmos = {22}},
            },
    }};
  }
  async::Executor executor_;
  sys::testing::ComponentContextProvider context_provider_;
};

TEST_F(MetricsUnitTest, Inspect) {
  CaptureSupplier cs(Template());
  cobalt::FakeLogger_Sync logger;
  sys::ComponentInspector inspector(context_provider_.context());
  Metrics m(
      kBucketMatches, zx::min(5), dispatcher(), &inspector, &logger,
      [&cs](Capture* c) { return cs.GetCapture(c, VMO, true /*use_capture_supplier_time*/); },
      [](const Capture& c, Digest* d) { Digester(kBucketMatches).Digest(c, d); });
  RunLoopUntil([&cs] { return cs.empty(); });

  // [START get_hierarchy]
  fpromise::result<inspect::Hierarchy> hierarchy;
  RunPromiseToCompletion(inspect::ReadFromInspector(*inspector.inspector())
                             .then([&](fpromise::result<inspect::Hierarchy>& result) {
                               hierarchy = std::move(result);
                             }));
  ASSERT_TRUE(hierarchy.is_ok());
  // [END get_hierarchy]

  // [START assertions]
  auto* metric_node = hierarchy.value().GetByPath({Metrics::kInspectPlatformNodeName});
  ASSERT_TRUE(metric_node);

  auto* metric_memory = metric_node->GetByPath({Metrics::kMemoryNodeName});
  ASSERT_TRUE(metric_memory);

  auto* usage_readings = metric_memory->node().get_property<inspect::UintPropertyValue>("Graphics");
  ASSERT_TRUE(usage_readings);
  EXPECT_EQ(2u, usage_readings->value());
}

TEST_F(MetricsUnitTest, All) {
  CaptureSupplier cs(Template());
  cobalt::FakeLogger_Sync logger;
  sys::ComponentInspector inspector(context_provider_.context());
  Metrics m(
      kBucketMatches, zx::msec(10), dispatcher(), &inspector, &logger,
      [&cs](Capture* c) { return cs.GetCapture(c, VMO, true /*use_capture_supplier_time*/); },
      [](const Capture& c, Digest* d) { Digester(kBucketMatches).Digest(c, d); });
  RunLoopUntil([&cs] { return cs.empty(); });
  // memory metric: 20 buckets + 4 (Orphaned, Kernel, Undigested and Free buckets)  +
  // memory_general_breakdown metric: 10 +
  // memory_leak metric: 10
  // = 44
  EXPECT_EQ(44U, logger.logged_events().size());
  using Breakdown = cobalt_registry::MemoryGeneralBreakdownMetricDimensionGeneralBreakdown;
  using Breakdown2 = cobalt_registry::MemoryLeakMetricDimensionGeneralBreakdown;
  for (const auto& cobalt_event : logger.logged_events()) {
    EXPECT_EQ(EventPayload::Tag::kMemoryBytesUsed, cobalt_event.payload.Which());
    switch (cobalt_event.metric_id) {
      case cobalt_registry::kMemoryMetricId:
        ASSERT_EQ(1u, cobalt_event.event_codes.size());
        switch (cobalt_event.event_codes[0]) {
          case MemoryMetricDimensionBucket::ZbiBuffer:
            EXPECT_EQ(1u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Graphics:
            EXPECT_EQ(2u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::ProtectedPool:
            EXPECT_EQ(3u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::ContiguousPool:
            EXPECT_EQ(4u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Fshost:
            EXPECT_EQ(5u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Minfs:
            EXPECT_EQ(6u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Blobfs:
            EXPECT_EQ(7u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::FlutterApps:
            EXPECT_EQ(8u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Flutter:
            EXPECT_EQ(9u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Web:
            EXPECT_EQ(21u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Kronk:
            EXPECT_EQ(12u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Scenic:
            EXPECT_EQ(13u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Amlogic:
            EXPECT_EQ(14u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Netstack:
            EXPECT_EQ(15u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Pkgfs:
            EXPECT_EQ(16u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Cast:
            EXPECT_EQ(17u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Archivist:
            EXPECT_EQ(18u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Cobalt:
            EXPECT_EQ(19u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Audio:
            EXPECT_EQ(20u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Context:
            EXPECT_EQ(21u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Undigested:
            EXPECT_EQ(22, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Orphaned:
            // 900 kmem.vmo - (1 + 2 + 3 + ... + 22) vmo digested in buckets = 647
            EXPECT_EQ(647, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Kernel:
            // 60 wired + 200 total_heap + 60 mmu_overhead + 10 ipc + 20 other = 350
            EXPECT_EQ(350u, cobalt_event.payload.memory_bytes_used());
            break;
          case MemoryMetricDimensionBucket::Free:
            EXPECT_EQ(800u, cobalt_event.payload.memory_bytes_used());
            break;
          default:
            ADD_FAILURE();
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
  sys::ComponentInspector inspector(context_provider_.context());
  Metrics m(
      kBucketMatches, zx::msec(10), dispatcher(), &inspector, &logger,
      [&cs](Capture* c) { return cs.GetCapture(c, VMO); },
      [](const Capture& c, Digest* d) { Digester(kBucketMatches).Digest(c, d); });
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
  sys::ComponentInspector inspector(context_provider_.context());
  Metrics m(
      kBucketMatches, zx::msec(10), dispatcher(), &inspector, &logger,
      [&cs](Capture* c) { return cs.GetCapture(c, VMO); },
      [](const Capture& c, Digest* d) { Digester(kBucketMatches).Digest(c, d); });
  RunLoopUntil([&cs] { return cs.empty(); });
  EXPECT_EQ(22U, logger.event_count());  // 2 + 10 + 10
}

}  // namespace
}  // namespace monitor
