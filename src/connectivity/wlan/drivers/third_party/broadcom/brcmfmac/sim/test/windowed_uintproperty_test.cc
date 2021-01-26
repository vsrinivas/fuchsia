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

#include <functional>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/sim/test/device_inspect_test.h"

namespace wlan {
namespace brcmfmac {

class UintPropertyTest : public DeviceInspectTestHelper {
 public:
  zx_status_t Init(zx_duration_t time_window, zx_duration_t refresh_interval) {
    zx_status_t status = count_.Init(&device_->inspect_.GetInspector().GetRoot(),
                                     time_window / refresh_interval, name_, 0);
    if (status != ZX_OK) {
      return status;
    }

    timer_ = std::make_unique<Timer>(device_->drvr()->bus_if, device_->drvr()->dispatcher,
                                     std::bind(&UintPropertyTest::TimerCallback, this), true);

    timer_->Start(refresh_interval);
    return ZX_OK;
  }

  void ScheduleIncrement(zx::duration delay, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
      env_->ScheduleNotification(std::bind(&UintPropertyTest::Increment, this), delay);
    }
  }

  uint64_t GetCount() {
    FetchHierarchy();
    auto* count = hierarchy_.value().node().get_property<inspect::UintPropertyValue>(name_);
    EXPECT_TRUE(count);
    return count->value();
  }

  void AddAndValidate(uint64_t inc, uint64_t value) {
    count_.Add(inc);
    count_.SlideWindow();
    EXPECT_EQ(value, GetCount());
  }

 protected:
  WindowedUintProperty count_;
  const std::string name_ = "uintproperty_counter";
  void Increment() { count_.Add(1); }

 private:
  std::unique_ptr<Timer> timer_;
  void TimerCallback() { count_.SlideWindow(); }
};

TEST_F(UintPropertyTest, InitErrors) {
  EXPECT_EQ(ZX_ERR_NO_RESOURCES,
            count_.Init(&device_->inspect_.GetInspector().GetRoot(), 129, name_, 0));
  EXPECT_EQ(ZX_OK, count_.Init(&device_->inspect_.GetInspector().GetRoot(), 128, name_, 0));
}

TEST_F(UintPropertyTest, SimpleCounter) {
  // Initialize object such that it builds a window_size of 5.
  EXPECT_EQ(ZX_OK, count_.Init(&device_->inspect_.GetInspector().GetRoot(), 5, name_, 0));

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

TEST_F(UintPropertyTest, CounterWithoutWindowUpdate) {
  // Initialize object such that it builds a queue_capacity of 5.
  EXPECT_EQ(ZX_OK, count_.Init(&device_->inspect_.GetInspector().GetRoot(), 5, name_, 0));

  // The test sequences over multiple increments happening between window
  // updates, to ensure the count is up to date. The sequence is as follows.
  //  - Increment by 1.
  //  - Slide window for one period.
  //  - Increment by 3, 1, 5 - ensure counter keeps up without window update.
  //  - Update window and ensure count remains unchanged.
  ASSERT_EQ(0u, GetCount());
  count_.Add(1);
  ASSERT_EQ(1u, GetCount());
  count_.SlideWindow();

  count_.Add(3);
  ASSERT_EQ(4u, GetCount());
  count_.Add(1);
  ASSERT_EQ(5u, GetCount());
  count_.Add(5);
  ASSERT_EQ(10u, GetCount());

  count_.SlideWindow();
  ASSERT_EQ(10u, GetCount());
}

TEST_F(UintPropertyTest, 24HrsCounter) {
  // Maintains count for a window of 24hours, refreshed every hour.
  EXPECT_EQ(ZX_OK, Init(ZX_HOUR(24), ZX_HOUR(1)));

  // Increment count once every hour, including the first and last.
  const uint32_t log_hours = 100;
  for (uint32_t i = 0; i <= log_hours; i++) {
    ScheduleIncrement(zx::hour(i), 1);
  }
  env_->Run(zx::hour(log_hours));

  // Since log_hours is > 24hrs, we expect the counter to show
  // a count of only 24.
  EXPECT_EQ(24u, GetCount());
}

TEST_F(UintPropertyTest, 60MinsCounter) {
  // Maintains count for a window of 60mins, refreshed every 10mins.
  EXPECT_EQ(ZX_OK, Init(ZX_MIN(60), ZX_MIN(10)));

  ScheduleIncrement(zx::min(30), 3);
  ScheduleIncrement(zx::min(80), 2);
  ScheduleIncrement(zx::min(81), 7);

  // We stop the test such that there has been some increments past the
  // last window update time, and ensure those counts are accounted for.
  env_->Run(zx::min(85));

  EXPECT_EQ(12u, GetCount());
}

}  // namespace brcmfmac
}  // namespace wlan
