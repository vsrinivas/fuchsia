// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/admin_test.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>

#include <algorithm>
#include <cstring>

#include "gtest/gtest.h"

namespace media::audio::drivers::test {

// For the channelization and sample_format that we've set, determine the size of each frame.
// This method assumes that SetFormat has already been sent to the driver.
void AdminTest::CalculateFrameSize() {
  EXPECT_LE(pcm_format_.valid_bits_per_sample, pcm_format_.bytes_per_sample * 8);
  frame_size_ = pcm_format_.number_of_channels * pcm_format_.bytes_per_sample;
}

void AdminTest::RequestRingBufferChannel() {
  fuchsia::hardware::audio::Format format = {};
  format.set_pcm_format(pcm_format_);

  fidl::InterfaceHandle<fuchsia::hardware::audio::RingBuffer> ring_buffer_handle;
  stream_config()->CreateRingBuffer(std::move(format), ring_buffer_handle.NewRequest());

  zx::channel channel = ring_buffer_handle.TakeChannel();
  ring_buffer_ =
      fidl::InterfaceHandle<fuchsia::hardware::audio::RingBuffer>(std::move(channel)).Bind();

  if (!stream_config().is_bound() || !ring_buffer_.is_bound()) {
    FAIL() << "Failed to get ring buffer channel";
  }

  AddErrorHandler(ring_buffer_, "RingBuffer");
}

// Request that driver set format to the lowest bit-rate/channelization of the ranges reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AdminTest::RequestMinFormat() {
  ASSERT_GT(pcm_formats().size(), 0u);

  // TODO(fxbug.dev/83792): Once driver issues are fixed, change this back to min_format()
  pcm_format_ = max_format();
  RequestRingBufferChannel();
  CalculateFrameSize();
}

// Request that driver set the highest bit-rate/channelization of the ranges reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AdminTest::RequestMaxFormat() {
  ASSERT_GT(pcm_formats().size(), 0u);

  pcm_format_ = max_format();
  RequestRingBufferChannel();
  CalculateFrameSize();
}

// Ring-buffer channel requests
//
// Request the FIFO depth in bytes, at the current format (relies on the ring buffer channel).
void AdminTest::RequestRingBufferProperties() {
  ring_buffer_->GetProperties(AddCallback(
      "RingBuffer::GetProperties", [this](fuchsia::hardware::audio::RingBufferProperties prop) {
        ring_buffer_props_ = std::move(prop);
      }));
  ExpectCallbacks();
  if (HasFailure()) {
    return;
  }

  ASSERT_TRUE(ring_buffer_props_.has_external_delay());
  EXPECT_GE(ring_buffer_props_.external_delay(), 0);

  EXPECT_TRUE(ring_buffer_props_.has_fifo_depth());

  EXPECT_TRUE(ring_buffer_props_.has_needs_cache_flush_or_invalidate());

  if (ring_buffer_props_.has_turn_on_delay()) {
    EXPECT_GE(ring_buffer_props_.turn_on_delay(), 0);
  }
}

// Request the ring buffer's VMO handle, at the current format (relies on the ring buffer channel).
void AdminTest::RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring) {
  min_ring_buffer_frames_ = min_ring_buffer_frames;
  notifications_per_ring_ = notifications_per_ring;
  zx::vmo ring_buffer_vmo;
  ring_buffer_->GetVmo(
      min_ring_buffer_frames, notifications_per_ring,
      AddCallback("GetVmo", [this, &ring_buffer_vmo](
                                fuchsia::hardware::audio::RingBuffer_GetVmo_Result result) {
        EXPECT_GE(result.response().num_frames, min_ring_buffer_frames_);
        ring_buffer_frames_ = result.response().num_frames;
        ring_buffer_vmo = std::move(result.response().ring_buffer);
        EXPECT_TRUE(ring_buffer_vmo.is_valid());
      }));
  ExpectCallbacks();
  if (HasFailure()) {
    return;
  }

  ring_buffer_mapper_.Unmap();
  const zx_vm_option_t option_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  EXPECT_EQ(ring_buffer_mapper_.CreateAndMap(ring_buffer_frames_ * frame_size_, option_flags,
                                             nullptr, &ring_buffer_vmo,
                                             ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER),
            ZX_OK);
}

void AdminTest::ActivateChannels(uint64_t active_channels_bitmask) {
  bool active_channels_were_set = false;

  auto send_time = zx::clock::get_monotonic();
  auto set_time = zx::time(0);
  ring_buffer_->SetActiveChannels(
      active_channels_bitmask,
      AddCallback("SetActiveChannels",
                  [active_channels_bitmask, &active_channels_were_set, &set_time](
                      fuchsia::hardware::audio::RingBuffer_SetActiveChannels_Result result) {
                    if (!result.is_err()) {
                      active_channels_were_set = true;
                      set_time = zx::time(result.response().set_time);
                    } else if (result.err() == ZX_ERR_NOT_SUPPORTED) {
                      GTEST_SKIP() << "This driver does not support SetActiveChannels()";
                    } else {
                      ADD_FAILURE() << "ring_buffer_fidl->SetActiveChannels(0x" << std::hex
                                    << active_channels_bitmask << ") received error " << std::dec
                                    << result.err();
                    }
                  }));
  ExpectCallbacks();
  if (!HasFailure() && !IsSkipped()) {
    EXPECT_GT(set_time, send_time);
  }
}

// Request that the driver start the ring buffer engine, responding with the start_time.
// This method assumes that GetVmo has previously been called and we are not already started.
void AdminTest::RequestStart() {
  // Any position notifications that arrive before the Start callback should cause failures.
  FailOnPositionNotifications();

  auto send_time = zx::clock::get_monotonic().get();
  ring_buffer_->Start(AddCallback("Start", [this](int64_t start_time) {
    AllowPositionNotifications();
    start_time_ = start_time;
  }));

  ExpectCallbacks();
  if (!HasFailure()) {
    EXPECT_GT(start_time_, send_time);
  }
}

// Request that the driver start the ring buffer engine, but expect disconnect rather than response.
void AdminTest::RequestStartAndExpectDisconnect(zx_status_t expected_error) {
  ring_buffer_->Start([](int64_t start_time) { FAIL() << "Received unexpected Start response"; });

  ExpectError(ring_buffer(), expected_error);
}

// Request that driver stop the ring buffer. This assumes that GetVmo has previously been called.
void AdminTest::RequestStop() {
  ring_buffer_->Stop(AddCallback("Stop"));

  ExpectCallbacks();
}

// After Stop is called, no position notification should be received.
// To validate this without any race windows: from within the next position notification itself,
// we call Stop and flag that subsequent position notifications should FAIL.
void AdminTest::RequestStopAndExpectNoPositionNotifications() {
  ring_buffer_->Stop(AddCallback("Stop", [this]() { FailOnPositionNotifications(); }));

  ExpectCallbacks();
}

// Request that the driver start the ring buffer engine, but expect disconnect rather than response.
// We would expect this if calling Stop before GetVmo, for example.
void AdminTest::RequestStopAndExpectDisconnect(zx_status_t expected_error) {
  ring_buffer_->Stop(AddUnexpectedCallback("Stop - expected disconnect instead"));

  ExpectError(ring_buffer(), expected_error);
}

// Start recording position/timestamps, set notifications to request another, and request the first
void AdminTest::EnablePositionNotifications() {
  record_position_info_ = true;
  request_next_position_notification_ = true;
  RequestPositionNotification();
}

void AdminTest::RequestPositionNotification() {
  ring_buffer_->WatchClockRecoveryPositionInfo(
      [this](fuchsia::hardware::audio::RingBufferPositionInfo position_info) {
        PositionNotificationCallback(position_info);
      });
}

void AdminTest::PositionNotificationCallback(
    fuchsia::hardware::audio::RingBufferPositionInfo position_info) {
  // If this is an unexpected callback, fail and exit.
  if (fail_on_position_notification_) {
    FAIL() << "Unexpected position notification";
  }

  EXPECT_GT(notifications_per_ring_, 0u) << "notifs_per_ring is 0";

  auto now = zx::clock::get_monotonic().get();
  EXPECT_LT(start_time_, now);
  EXPECT_LT(position_info.timestamp, now);

  if (position_notification_count_) {
    EXPECT_GT(position_info.timestamp, start_time_);
    EXPECT_GT(position_info.timestamp, position_info_.timestamp);
  } else {
    EXPECT_GE(position_info.timestamp, start_time_);
  }
  EXPECT_LT(position_info.position, ring_buffer_frames_ * frame_size_);

  // If we want to continue to chain of position notifications, request the next one.
  if (request_next_position_notification_) {
    RequestPositionNotification();
  }

  // If we don't need to update our running stats on position, exit now.
  if (!record_position_info_) {
    return;
  }

  ++position_notification_count_;
  running_position_ += position_info.position;
  running_position_ -= position_info_.position;

  if (position_info.position <= position_info_.position) {
    running_position_ += (ring_buffer_frames_ * frame_size_);
  }
  position_info_.timestamp = position_info.timestamp;
  position_info_.position = position_info.position;
}

// Wait for the specified number of position notifications, then stop recording timestamp data.
// ...but don't DisablePositionNotifications, in case later notifications surface other issues.
void AdminTest::ExpectPositionNotifyCount(uint32_t count) {
  RunLoopUntil([this, count]() { return position_notification_count_ >= count || HasFailure(); });

  record_position_info_ = false;
}

void AdminTest::ValidatePositionInfo() {
  auto timestamp_duration = position_info_.timestamp - start_time_;
  auto observed_duration = zx::clock::get_monotonic().get() - start_time_;

  ASSERT_GT(position_notification_count_, 0u) << "No position notifications received";
  ASSERT_GT(notifications_per_ring_, 0u) << "notifications_per_ring_ cannot be zero";

  // What timestamp do we expect, for the final notification received? We know how many
  // notifications we've received; we'll multiply this by the per-notification time duration.
  // However, upon enabling notifications, our first notification might arrive immediately. Thus,
  // the average number of notification periods elapsed is (position_notification_count_ - 0.5).
  ASSERT_NE(pcm_format_.frame_rate * notifications_per_ring_, 0u);
  auto ns_per_notification =
      (zx::sec(1) * ring_buffer_frames_) / (pcm_format_.frame_rate * notifications_per_ring_);
  double average_num_notif_periods_elapsed = position_notification_count_ - 0.5;

  // Furthermore, notification timing requirements for drivers are somewhat loose, so we include
  // a tolerance range of +/- 2. notification periods.
  auto expected_time = ns_per_notification.get() * average_num_notif_periods_elapsed;
  auto timing_tolerance = ns_per_notification.get() * 2;
  auto min_allowed_time = expected_time - timing_tolerance;
  auto max_allowed_time = expected_time + timing_tolerance;

  EXPECT_GE(timestamp_duration, min_allowed_time)
      << "Notification rate too high. Device clock rate too fast?";
  EXPECT_LE(timestamp_duration, max_allowed_time)
      << "Notification rate too low. Device clock rate too slow?";

  // Also validate when the notification was actually received (not just the timestamp).
  EXPECT_GT(observed_duration, min_allowed_time);
}

#define DEFINE_ADMIN_TEST_CLASS(CLASS_NAME, CODE)                               \
  class CLASS_NAME : public AdminTest {                                         \
   public:                                                                      \
    explicit CLASS_NAME(const DeviceEntry& dev_entry) : AdminTest(dev_entry) {} \
    void TestBody() override { CODE }                                           \
  }

// For now, certain test cases fail on a2dp-source. Skip them and complain (don't silently pass).
#define SKIP_IF_A2DP                                                                   \
  if (device_entry().dir_fd == DeviceEntry::kA2dp) {                                   \
    GTEST_SKIP() << "*** Bluetooth A2DP does not support this test at this time. ***"; \
  }

//
// Test cases that target each of the various admin commands
//
// Any case not ending in disconnect/error should WaitForError, in case the channel disconnects.

// Verify valid responses: ring buffer properties
DEFINE_ADMIN_TEST_CLASS(GetRingBufferProperties, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMaxFormat());

  RequestRingBufferProperties();
  WaitForError();
});

// Verify valid responses: get ring buffer VMO.
DEFINE_ADMIN_TEST_CLASS(GetBuffer, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMinFormat());

  RequestBuffer(100, 1);
  WaitForError();
});

// Verify valid responses: set active channels
DEFINE_ADMIN_TEST_CLASS(SetActiveChannels, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMaxFormat());
  ASSERT_NO_FAILURE_OR_SKIP(ActivateChannels(0));

  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(8000, 32));
  ASSERT_NO_FAILURE_OR_SKIP(RequestStart());

  auto all_channels = (1 << pcm_format().number_of_channels) - 1;
  ActivateChannels(all_channels);
  WaitForError();
});

// Verify that valid start responses are received.
DEFINE_ADMIN_TEST_CLASS(Start, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMinFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(32000, 4));

  RequestStart();
  WaitForError();
});

// ring-buffer FIDL channel should disconnect, with ZX_ERR_BAD_STATE
DEFINE_ADMIN_TEST_CLASS(StartBeforeGetVmoShouldDisconnect, {
  // TODO(fxbug.dev/66431): fix a2dp-source and enable these test cases for all a2dp devices.
  SKIP_IF_A2DP;

  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMinFormat());

  RequestStartAndExpectDisconnect(ZX_ERR_BAD_STATE);
});

// ring-buffer FIDL channel should disconnect, with ZX_ERR_BAD_STATE
DEFINE_ADMIN_TEST_CLASS(StartWhileStartedShouldDisconnect, {
  // TODO(fxbug.dev/66431): fix a2dp-source and enable these test cases for all devices.
  SKIP_IF_A2DP;

  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMaxFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(8000, 32));
  ASSERT_NO_FAILURE_OR_SKIP(RequestStart());

  RequestStartAndExpectDisconnect(ZX_ERR_BAD_STATE);
});

// Verify that valid stop responses are received.
DEFINE_ADMIN_TEST_CLASS(Stop, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMaxFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(100, 3));
  ASSERT_NO_FAILURE_OR_SKIP(RequestStart());

  RequestStop();
  WaitForError();
});

// ring-buffer FIDL channel should disconnect, with ZX_ERR_BAD_STATE
DEFINE_ADMIN_TEST_CLASS(StopBeforeGetVmoShouldDisconnect, {
  // TODO(fxbug.dev/66431): fix a2dp-source and enable these test cases for all devices.
  SKIP_IF_A2DP;

  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMinFormat());

  RequestStopAndExpectDisconnect(ZX_ERR_BAD_STATE);
});

DEFINE_ADMIN_TEST_CLASS(StopWhileStoppedIsPermitted, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMinFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(100, 1));
  ASSERT_NO_FAILURE_OR_SKIP(RequestStop());

  RequestStop();
  WaitForError();
});

// Verify position notifications at fast (64/sec) rate.
DEFINE_ADMIN_TEST_CLASS(PositionNotifyFast, {
  // TODO(fxbug.dev/66431): fix a2dp-source and enable these test cases for all devices.
  SKIP_IF_A2DP;

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

  // // We can stop enqueuing additional position notifications now
  // DisablePositionNotifications();
  WaitForError();
});

// Verify position notifications at slow (1/sec) rate.
DEFINE_ADMIN_TEST_CLASS(PositionNotifySlow, {
  // TODO(fxbug.dev/66431): fix a2dp-source and enable these test cases for all devices.
  SKIP_IF_A2DP;

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

  // // We can stop enqueuing additional position notifications now
  // DisablePositionNotifications();
  // Wait longer than the default (100 ms), as notifications are less frequent than that.
  zx::duration time_per_notif =
      zx::sec(1) * ring_buffer_frames() / pcm_format().frame_rate / kNotifsPerRingBuffer;
  WaitForError(time_per_notif);
});

// Verify no position notifications arrive after stop.
DEFINE_ADMIN_TEST_CLASS(NoPositionNotifyAfterStop, {
  // TODO(fxbug.dev/66431): fix a2dp-source and enable these test cases for all devices.
  SKIP_IF_A2DP;

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
DEFINE_ADMIN_TEST_CLASS(PositionNotifyNone, {
  ASSERT_NO_FAILURE_OR_SKIP(RequestFormats());
  ASSERT_NO_FAILURE_OR_SKIP(RequestMaxFormat());
  ASSERT_NO_FAILURE_OR_SKIP(RequestBuffer(8000, 0));
  ASSERT_NO_FAILURE_OR_SKIP(FailOnPositionNotifications());
  ASSERT_NO_FAILURE_OR_SKIP(EnablePositionNotifications());

  RequestStart();
  WaitForError();
});

// Register separate test case instances for each enumerated device
//
// See googletest/docs/advanced.md for details
#define REGISTER_ADMIN_TEST(CLASS_NAME, DEVICE)                                              \
  testing::RegisterTest("AdminTest", TestNameForEntry(#CLASS_NAME, DEVICE).c_str(), nullptr, \
                        DevNameForEntry(DEVICE).c_str(), __FILE__, __LINE__,                 \
                        [=]() -> AdminTest* { return new CLASS_NAME(DEVICE); })

void RegisterAdminTestsForDevice(const DeviceEntry& device_entry,
                                 bool expect_audio_core_connected) {
  // If audio_core is connected to the audio driver, admin tests will fail.
  // We test a hermetic instance of the A2DP driver, so audio_core is never connected.
  if (device_entry.dir_fd == DeviceEntry::kA2dp || !expect_audio_core_connected) {
    REGISTER_ADMIN_TEST(GetRingBufferProperties, device_entry);
    REGISTER_ADMIN_TEST(GetBuffer, device_entry);

    REGISTER_ADMIN_TEST(SetActiveChannels, device_entry);

    REGISTER_ADMIN_TEST(Start, device_entry);
    REGISTER_ADMIN_TEST(Stop, device_entry);

    REGISTER_ADMIN_TEST(StartBeforeGetVmoShouldDisconnect, device_entry);
    REGISTER_ADMIN_TEST(StartWhileStartedShouldDisconnect, device_entry);

    REGISTER_ADMIN_TEST(StopBeforeGetVmoShouldDisconnect, device_entry);
    REGISTER_ADMIN_TEST(StopWhileStoppedIsPermitted, device_entry);

    REGISTER_ADMIN_TEST(PositionNotifyFast, device_entry);
    REGISTER_ADMIN_TEST(PositionNotifySlow, device_entry);
    REGISTER_ADMIN_TEST(NoPositionNotifyAfterStop, device_entry);
    REGISTER_ADMIN_TEST(PositionNotifyNone, device_entry);
  }
}

}  // namespace media::audio::drivers::test
