/*
 * Copyright (c) 2021 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/inspect/windowed_uint_property.h"

#include <lib/async/cpp/task.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/zx/time.h>

#include <functional>
#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/test/device_inspect_test_utils.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "zircon/errors.h"

namespace wlan::brcmfmac {

using ::testing::NotNull;

class WindowedUintPropertyTest : public gtest::TestLoopFixture {
 public:
  zx_status_t Init(zx::duration time_window, zx::duration refresh_interval) {
    const size_t window_size = time_window / refresh_interval;
    if (window_size > std::numeric_limits<uint32_t>::max()) {
      return ZX_ERR_INVALID_ARGS;
    }
    zx_status_t status =
        count_.Init(&inspector_.GetRoot(), static_cast<uint32_t>(window_size), name_, 0);
    if (status != ZX_OK) {
      return status;
    }

    async::PostDelayedTask(
        dispatcher(),
        std::bind(&WindowedUintPropertyTest::SlideWindowTimer, this, refresh_interval),
        refresh_interval);
    return ZX_OK;
  }

  void SlideWindowTimer(zx::duration refresh_interval) {
    count_.SlideWindow();
    async::PostDelayedTask(
        dispatcher(),
        std::bind(&WindowedUintPropertyTest::SlideWindowTimer, this, refresh_interval),
        refresh_interval);
  }

  void ScheduleIncrement(zx::duration delay, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
      async::PostDelayedTask(
          dispatcher(), [this]() { count_.Add(1); }, delay);
    }
  }

  void GetCount(uint64_t* out_count) {
    ASSERT_THAT(out_count, NotNull());
    auto hierarchy = FetchHierarchy(inspector_);
    auto* count = hierarchy.value().node().get_property<inspect::UintPropertyValue>(name_);
    ASSERT_THAT(count, NotNull());
    *out_count = count->value();
  }

  void AddAndValidate(uint64_t inc, uint64_t value) {
    count_.Add(inc);
    count_.SlideWindow();
    uint64_t count;
    GetCount(&count);
    EXPECT_EQ(value, count);
  }

 protected:
  inspect::Inspector inspector_;
  WindowedUintProperty count_;
  const std::string name_ = "uint_property_counter";
};

TEST_F(WindowedUintPropertyTest, InitErrors) {
  EXPECT_EQ(ZX_ERR_NO_RESOURCES, count_.Init(&inspector_.GetRoot(), 129, name_, 0));
  EXPECT_EQ(ZX_OK, count_.Init(&inspector_.GetRoot(), 128, name_, 0));
}

TEST_F(WindowedUintPropertyTest, SimpleCounter) {
  // Initialize object such that it builds a window_size of 5.
  EXPECT_EQ(ZX_OK, count_.Init(&inspector_.GetRoot(), 5, name_, 0));

  // We sequence over the following increments per interval time, and ensure the
  // value is always the sum of the last 5 increments.
  // [6, 8, 3, 0, 2, 0, 0, 1, 9, 15, 0, 1, 0, 0, 0, 0, 0]
  AddAndValidate(6, 6);
  AddAndValidate(8, 14);
  AddAndValidate(3, 17);
  AddAndValidate(0, 17);
  AddAndValidate(2, 19);
  AddAndValidate(0, 13);   // 6 falls off the queue.
  AddAndValidate(0, 5);    // 8 falls off the queue.
  AddAndValidate(1, 3);    // 3 falls off the queue, and 1 gets added.
  AddAndValidate(9, 12);   // 0 falls off the queue, and 9 gets added.
  AddAndValidate(15, 25);  // 2 falls off the queue, and 15 gets added.
  AddAndValidate(0, 25);   // 0 falls off the queue, and 0 gets added.
  AddAndValidate(1, 26);   // 0 falls off the queue, and 1 gets added.
  AddAndValidate(0, 25);   // 1 falls off the queue, and 0 gets added.
  AddAndValidate(0, 16);   // 9 falls off the queue, and 0 gets added.
  AddAndValidate(0, 1);    // 15 falls off the queue, and 0 gets added.
  AddAndValidate(0, 1);    // 0 falls off the queue, and 0 gets added.
  AddAndValidate(0, 0);    // 1 falls off the queue, and 0 gets added.
}

TEST_F(WindowedUintPropertyTest, CounterWithoutWindowUpdate) {
  // Initialize object such that it builds a queue_capacity of 5.
  EXPECT_EQ(ZX_OK, count_.Init(&inspector_.GetRoot(), 5, name_, 0));

  // The test sequences over multiple increments happening between window updates, to ensure the
  // count is up to date. The sequence is as follows.
  //  - Increment by 1.
  //  - Slide window for one period.
  //  - Increment by 3, 1, 5 - ensure counter keeps up without window update.
  //  - Update window and ensure count remains unchanged.
  uint64_t count;
  GetCount(&count);
  ASSERT_EQ(0u, count);
  count_.Add(1);
  GetCount(&count);
  ASSERT_EQ(1u, count);
  count_.SlideWindow();

  count_.Add(3);
  GetCount(&count);
  ASSERT_EQ(4u, count);
  count_.Add(1);
  GetCount(&count);
  ASSERT_EQ(5u, count);
  count_.Add(5);
  GetCount(&count);
  ASSERT_EQ(10u, count);

  count_.SlideWindow();
  GetCount(&count);
  ASSERT_EQ(10u, count);
}

TEST_F(WindowedUintPropertyTest, 24HrsCounter) {
  // Maintains count for a window of 24hours, refreshed every hour.
  EXPECT_EQ(ZX_OK, Init(zx::hour(24), zx::hour(1)));

  // Increment count once every hour, including the first and last.
  constexpr zx::duration kLogHours = zx::hour(100);
  for (zx::duration i; i <= kLogHours; i += zx::hour(1)) {
    ScheduleIncrement(i, 1);
  }
  RunLoopFor(kLogHours);

  // Since kLogHours is > 24hrs, we expect the counter to show a count of only 24.
  uint64_t count;
  GetCount(&count);
  EXPECT_EQ(24u, count);
}

TEST_F(WindowedUintPropertyTest, 60MinsCounter) {
  // Maintains count for a window of 60mins, refreshed every 10mins.
  EXPECT_EQ(ZX_OK, Init(zx::min(60), zx::min(10)));

  ScheduleIncrement(zx::min(30), 3);
  ScheduleIncrement(zx::min(80), 2);
  ScheduleIncrement(zx::min(81), 7);

  // We stop the test such that there has been some increments past the last window update time, and
  // ensure those counts are accounted for.
  RunLoopFor(zx::min(85));

  uint64_t count;
  GetCount(&count);
  EXPECT_EQ(12u, count);
}

}  // namespace wlan::brcmfmac
