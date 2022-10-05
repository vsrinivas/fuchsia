// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/lib/guest_test.h"

namespace {

constexpr size_t kVirtioBalloonPageCount = 256;

template <class T>
class BalloonGuestTest : public GuestTest<T> {
 public:
  void TestGetMemStats(
      const char* trace_context,
      const fuchsia::virtualization::BalloonControllerSyncPtr& balloon_controller) {
    SCOPED_TRACE(trace_context);
    // 5.5.6.4 Memory Statistics Tags
    constexpr uint16_t VIRTIO_BALLOON_S_MEMFREE = 4;
    constexpr uint16_t VIRTIO_BALLOON_S_MEMTOT = 5;
    constexpr uint16_t VIRTIO_BALLOON_S_AVAIL = 6;
    ::fidl::VectorPtr<::fuchsia::virtualization::MemStat> mem_stats;
    int32_t mem_stats_status = 0;
    zx_status_t status = balloon_controller->GetMemStats(&mem_stats_status, &mem_stats);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(mem_stats_status, ZX_OK);
    std::unordered_map<uint16_t, uint64_t> stats;
    for (auto& el : mem_stats.value()) {
      stats[el.tag] = el.val;
    }
    EXPECT_GT(stats[VIRTIO_BALLOON_S_MEMTOT], 0u);
    EXPECT_GT(stats[VIRTIO_BALLOON_S_MEMFREE], 0u);
    EXPECT_GT(stats[VIRTIO_BALLOON_S_AVAIL], 0u);
    EXPECT_LE(stats[VIRTIO_BALLOON_S_MEMFREE], stats[VIRTIO_BALLOON_S_MEMTOT]);
    EXPECT_LE(stats[VIRTIO_BALLOON_S_AVAIL], stats[VIRTIO_BALLOON_S_MEMTOT]);
  }
};

// Zircon does not yet have a virtio balloon driver.
using GuestTypes = ::testing::Types<DebianEnclosedGuest, TerminaEnclosedGuest>;
TYPED_TEST_SUITE(BalloonGuestTest, GuestTypes, GuestTestNameGenerator);

TYPED_TEST(BalloonGuestTest, ConnectDisconnect) {
  std::string result;
  EXPECT_EQ(this->Execute({"echo", "test"}, &result), ZX_OK);
  EXPECT_EQ(result, "test\n");

  fuchsia::virtualization::BalloonControllerSyncPtr balloon_controller;
  ASSERT_TRUE(this->ConnectToBalloon(balloon_controller.NewRequest()));

  uint32_t initial_num_pages;
  uint32_t requested_num_pages;
  zx_status_t status = balloon_controller->GetBalloonSize(&initial_num_pages, &requested_num_pages);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(requested_num_pages, initial_num_pages);
  this->TestGetMemStats("Before inflate", balloon_controller);

  // Request an increase to the number of pages in the balloon.
  status = balloon_controller->RequestNumPages(initial_num_pages + kVirtioBalloonPageCount);
  ASSERT_EQ(status, ZX_OK);

  // Verify that the number of pages eventually equals the requested number. The
  // guest may not respond to the request immediately so we call GetBalloonSize in
  // a loop.
  uint32_t current_num_pages;
  while (true) {
    status = balloon_controller->GetBalloonSize(&current_num_pages, &requested_num_pages);
    ASSERT_EQ(status, ZX_OK);
    EXPECT_EQ(requested_num_pages, initial_num_pages + kVirtioBalloonPageCount);
    if (current_num_pages == initial_num_pages + kVirtioBalloonPageCount) {
      break;
    }
  }
  this->TestGetMemStats("After inflate", balloon_controller);

  // Request a decrease to the number of pages in the balloon back to the
  // initial value.
  status = balloon_controller->RequestNumPages(initial_num_pages);
  ASSERT_EQ(status, ZX_OK);

  while (true) {
    status = balloon_controller->GetBalloonSize(&current_num_pages, &requested_num_pages);
    ASSERT_EQ(status, ZX_OK);
    EXPECT_EQ(requested_num_pages, initial_num_pages);
    if (current_num_pages == initial_num_pages) {
      break;
    }
  }
  this->TestGetMemStats("After deflate", balloon_controller);
}

}  // namespace
