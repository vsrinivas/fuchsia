// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/position_test.h"

#include <lib/media/cpp/timeline_rate.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <zircon/compiler.h>

#include <algorithm>
#include <cstring>

#include "gtest/gtest.h"

namespace media::audio::drivers::test {

// Start recording position/timestamps, set notifications to request another, and request the first
void PositionTest::EnablePositionNotifications() {
  record_position_info_ = true;
  request_next_position_notification_ = true;
  RequestPositionNotification();
}

void PositionTest::RequestPositionNotification() {
  ring_buffer()->WatchClockRecoveryPositionInfo(
      [this](fuchsia::hardware::audio::RingBufferPositionInfo position_info) {
        PositionNotificationCallback(position_info);
      });
}

void PositionTest::PositionNotificationCallback(
    fuchsia::hardware::audio::RingBufferPositionInfo position_info) {
  AdminTest::PositionNotificationCallback(position_info);

  EXPECT_TRUE(position_notification_is_expected_);

  zx::time now = zx::clock::get_monotonic();
  zx::time position_time = zx::time(position_info.timestamp);
  EXPECT_LT(start_time(), now);
  EXPECT_LT(position_time, now);

  if (position_notification_count_) {
    EXPECT_GT(position_time, start_time());
    EXPECT_GT(position_time, zx::time(saved_position_.timestamp));
  } else {
    EXPECT_GE(position_time, start_time());
  }
  EXPECT_LT(position_info.position, ring_buffer_frames() * frame_size());

  // If we want to continue to chain of position notifications, request the next one.
  if (request_next_position_notification_) {
    RequestPositionNotification();
  }

  // If we don't need to update our running stats on position, exit now.
  if (!record_position_info_) {
    return;
  }

  ++position_notification_count_;

  // The `.position` reported by a position notification is a byte position within the ring buffer.
  // For long-running byte position, we could maintain a `running_position_` (a uint64_t initialized
  // to 0 upon Start()) that is updated by the algorithm below. This uses `.position` as a ring
  // "modulo" and adds the buffer size when it detects rollover, so it does not account for "sparse"
  // position notifications that occur more than a ring-buffer apart. For this technique to be
  // accurate, the ring-buffer client must (1) set position notification frequency to 2/buffer or
  // greater and (2) register for notifications actively enough that the position advanced between
  // notifications never exceeds the ring-buffer size.
  //   running_position_ += position_info.position;
  //   running_position_ -= saved_position_.position;
  //   if (position_info.position <= saved_position_.position) {
  //     running_position_ += (ring_buffer_frames() * frame_size());
  //   }

  saved_position_.timestamp = position_info.timestamp;
  saved_position_.position = position_info.position;
}

// Wait for the specified number of position notifications, then stop recording timestamp data.
// ...but don't DisablePositionNotifications, in case later notifications surface other issues.
void PositionTest::ExpectPositionNotifyCount(uint32_t count) {
  RunLoopUntil([this, count]() { return position_notification_count_ >= count || HasFailure(); });

  record_position_info_ = false;
}

// What timestamp do we expect, for the final notification received? We know how many
// notifications we've received; we'll multiply this by the per-notification time duration.
void PositionTest::ValidatePositionInfo() {
  zx::duration notification_timestamp = zx::time(saved_position_.timestamp) - start_time();
  zx::duration observed_timestamp = zx::clock::get_monotonic() - start_time();

  ASSERT_GT(position_notification_count_, 0u) << "No position notifications received";
  ASSERT_GT(pcm_format().frame_rate, 0u) << "Frame rate cannot be zero";

  // ns/notification = nsec/sec * sec/frames * frames/ring * ring/notification
  auto ns_per_notification = TimelineRate::NsPerSecond / TimelineRate(pcm_format().frame_rate) *
                             TimelineRate(ring_buffer_frames()) /
                             TimelineRate(notifications_per_ring());

  // Upon enabling notifications, our first notification might arrive immediately. Thus, the average
  // number of notification periods elapsed is (position_notification_count_ - 0.5).
  auto expected_timestamp = zx::duration(ns_per_notification.Scale(position_notification_count_) -
                                         ns_per_notification.Scale(1) / 2);

  // Delivery-time requirements for pos notifications are loose; include a tolerance of +/-2 notifs.
  auto timestamp_tolerance = zx::duration(ns_per_notification.Scale(2));
  auto min_allowed_timestamp = expected_timestamp - timestamp_tolerance;
  auto max_allowed_timestamp = expected_timestamp + timestamp_tolerance;

  EXPECT_GE(notification_timestamp, min_allowed_timestamp)
      << notification_timestamp.to_nsecs() << " less than min " << min_allowed_timestamp.to_nsecs()
      << ". Notification rate too high. Device clock rate too fast?";
  EXPECT_LE(notification_timestamp, max_allowed_timestamp)
      << notification_timestamp.to_nsecs() << " exceeds max " << max_allowed_timestamp.to_nsecs()
      << ". Notification rate too low. Device clock rate too slow?";

  // Also validate when the notification was actually received (not just the timestamp).
  EXPECT_GT(observed_timestamp, min_allowed_timestamp);
}

#define DEFINE_POSITION_TEST_CLASS(CLASS_NAME, CODE)                               \
  class CLASS_NAME : public PositionTest {                                         \
   public:                                                                         \
    explicit CLASS_NAME(const DeviceEntry& dev_entry) : PositionTest(dev_entry) {} \
    void TestBody() override { CODE }                                              \
  }

//
// Test cases that target each of the various admin commands
//
// Any case not ending in disconnect/error should WaitForError, in case the channel disconnects.

// Verify position notifications at fast (64/sec) rate.
DEFINE_POSITION_TEST_CLASS(PositionNotifyFast, {
  // Request a 0.5-second ring-buffer
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMaxFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(pcm_format().frame_rate / 2, 32));
  ASSERT_NO_FAILURE_OR_SKIP(EnablePositionNotifications());
  ASSERT_NO_FAILURE_OR_SKIP(RequestStart());

  // After an arbitrary number of notifications, stop updating the position info but allow
  // notifications to continue. Analyze whether the position advance meets expectations.
  ExpectPositionNotifyCount(16u);
  ValidatePositionInfo();

  WaitForError();
});

// Verify position notifications at slow (1/sec) rate.
DEFINE_POSITION_TEST_CLASS(PositionNotifySlow, {
  // Request a 2-second ring-buffer
  constexpr auto kNotifsPerRingBuffer = 2u;
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMinFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(pcm_format().frame_rate * 2, kNotifsPerRingBuffer));
  ASSERT_NO_FAILURE_OR_SKIP(EnablePositionNotifications());
  ASSERT_NO_FAILURE_OR_SKIP(RequestStart());

  // After an arbitrary number of notifications, stop updating the position info but allow
  // notifications to continue. Analyze whether the position advance meets expectations.
  ExpectPositionNotifyCount(3u);
  ValidatePositionInfo();

  // Wait longer than the default (100 ms), as notifications are less frequent than that.
  zx::duration time_per_notif =
      zx::sec(1) * ring_buffer_frames() / pcm_format().frame_rate / kNotifsPerRingBuffer;
  WaitForError(time_per_notif);
});

// Verify no position notifications arrive after stop.
DEFINE_POSITION_TEST_CLASS(NoPositionNotifyAfterStop, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMaxFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(8000, 32));
  ASSERT_NO_FAILURE_OR_SKIP(EnablePositionNotifications());
  ASSERT_NO_FAILURE_OR_SKIP(RequestStart());
  ASSERT_NO_FAILURE_OR_SKIP(ExpectPositionNotifyCount(3u));

  RequestStopAndExpectNoPositionNotifications();
  WaitForError();
});

// Verify no position notifications arrive if notifications_per_ring is 0.
DEFINE_POSITION_TEST_CLASS(PositionNotifyNone, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMaxFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(8000, 0));
  ASSERT_NO_FAILURE_OR_SKIP(DisallowPositionNotifications());
  ASSERT_NO_FAILURE_OR_SKIP(EnablePositionNotifications());

  RequestStart();
  WaitForError();
});

// Register separate test case instances for each enumerated device
//
// See googletest/docs/advanced.md for details
#define REGISTER_POSITION_TEST(CLASS_NAME, DEVICE)                                              \
  testing::RegisterTest("PositionTest", TestNameForEntry(#CLASS_NAME, DEVICE).c_str(), nullptr, \
                        DevNameForEntry(DEVICE).c_str(), __FILE__, __LINE__,                    \
                        [=]() -> PositionTest* { return new CLASS_NAME(DEVICE); })

#define REGISTER_DISABLED_POSITION_TEST(CLASS_NAME, DEVICE)                                       \
  testing::RegisterTest(                                                                          \
      "PositionTest", (std::string("DISABLED_") + TestNameForEntry(#CLASS_NAME, DEVICE)).c_str(), \
      nullptr, DevNameForEntry(DEVICE).c_str(), __FILE__, __LINE__,                               \
      [=]() -> PositionTest* { return new CLASS_NAME(DEVICE); })

void RegisterPositionTestsForDevice(const DeviceEntry& device_entry,
                                    bool expect_audio_core_connected, bool enable_position_tests) {
  // If audio_core is connected to the audio driver, admin tests will fail.
  // We test a hermetic instance of the A2DP driver, so audio_core is never connected.
  if (device_entry.dir_fd == DeviceEntry::kA2dp || !expect_audio_core_connected) {
    if (enable_position_tests) {
      REGISTER_POSITION_TEST(PositionNotifyFast, device_entry);
      REGISTER_POSITION_TEST(PositionNotifySlow, device_entry);
      REGISTER_POSITION_TEST(NoPositionNotifyAfterStop, device_entry);
      REGISTER_POSITION_TEST(PositionNotifyNone, device_entry);
    } else {
      REGISTER_DISABLED_POSITION_TEST(PositionNotifyFast, device_entry);
      REGISTER_DISABLED_POSITION_TEST(PositionNotifySlow, device_entry);
      REGISTER_DISABLED_POSITION_TEST(NoPositionNotifyAfterStop, device_entry);
      REGISTER_DISABLED_POSITION_TEST(PositionNotifyNone, device_entry);
    }
  }
}

}  // namespace media::audio::drivers::test
