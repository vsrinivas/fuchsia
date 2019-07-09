// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/metrics/watcher.h"

#include <gtest/gtest.h>
#include <lib/gtest/test_loop_fixture.h>
#include <zircon/types.h>

#include "lib/gtest/real_loop_fixture.h"
#include "src/developer/memory/metrics/tests/test_utils.h"

namespace memory {
namespace test {

using WatcherUnitTest = gtest::RealLoopFixture;

class CaptureSupplier {
 public:
  explicit CaptureSupplier(std::vector<CaptureTemplate> templates)
      : templates_(templates), index_(0) {}

  zx_status_t GetCapture(Capture& capture, CaptureLevel level) {
    auto& t = templates_.at(index_);
    t.time = index_++;
    TestUtils::CreateCapture(capture, t);
    return ZX_OK;
  }

  bool empty() const { return index_ == templates_.size(); }

 private:
  std::vector<CaptureTemplate> templates_;
  size_t index_;
};

TEST_F(WatcherUnitTest, Initial) {
  // Confirms the basic case, that we get an initial high water memory
  // mark, and that we get the process and vmo details.
  CaptureSupplier cs({{
                          .kmem = {.free_bytes = 100},
                      },
                      {.kmem = {.free_bytes = 100},
                       .vmos =
                           {
                               {.koid = 1, .name = "v1", .committed_bytes = 101},
                           },
                       .processes = {
                           {.koid = 2, .name = "p1", .vmos = {1}},
                       }}});
  std::vector<Capture> high_waters;
  Watcher w(
      zx::duration(ZX_MSEC(1)), 100, dispatcher(),
      [&cs](Capture& c, CaptureLevel l) { return cs.GetCapture(c, l); },
      [&high_waters](const Capture& c) { high_waters.push_back(c); });
  w.Run();
  ASSERT_EQ(1U, high_waters.size());
  ASSERT_EQ(1U, high_waters.size());
  auto const& c = high_waters.at(0);
  EXPECT_EQ(1U, c.time());
  EXPECT_EQ(100U, c.kmem().free_bytes);
  EXPECT_EQ(1U, c.koid_to_process().size());
  EXPECT_EQ(1U, c.koid_to_vmo().size());
}

TEST_F(WatcherUnitTest, TwoHighs) {
  // Check that we can exceed the highwater twice.
  CaptureSupplier cs({{
                          .kmem = {.free_bytes = 200},
                      },
                      {
                          .kmem = {.free_bytes = 200},
                      },
                      {
                          .kmem = {.free_bytes = 150},
                      },
                      {
                          .kmem = {.free_bytes = 150},
                      },
                      {
                          .kmem = {.free_bytes = 100},
                      },
                      {
                          .kmem = {.free_bytes = 100},
                      }});
  std::vector<Capture> high_waters;
  Watcher w(
      zx::duration(ZX_MSEC(1)), 100, dispatcher(),
      [&cs](Capture& c, CaptureLevel l) { return cs.GetCapture(c, l); },
      [&high_waters](const Capture& c) { high_waters.push_back(c); });
  w.Run();
  RunLoopUntil([&cs] { return cs.empty(); }, zx::duration::infinite());
  ASSERT_EQ(2U, high_waters.size());
  EXPECT_EQ(200U, high_waters.at(0).kmem().free_bytes);
  EXPECT_EQ(100U, high_waters.at(1).kmem().free_bytes);
}

}  // namespace test
}  // namespace memory
