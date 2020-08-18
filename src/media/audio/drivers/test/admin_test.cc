// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/admin_test.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>

#include <algorithm>
#include <cstring>

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::drivers::test {

// static
bool AdminTest::device_access_denied_[2] = {false, false};

// Whenever the test group starts, try anew to get admin access (needed for "--gtest_repeat")
void AdminTest::SetUpTestSuite() {
  AdminTest::device_access_denied_[DeviceType::Input] = false;
  AdminTest::device_access_denied_[DeviceType::Output] = false;
}

// If earlier in this test group we failed to get admin access, exit immediately.
// If the admin flag is specified, then fail; otherwise, skip the test case.
void AdminTest::SetUp() {
  TestBase::SetUp();

  // If previous test in this group found no device, we don't need to search again - skip this test.
  if (no_devices_found()) {
    GTEST_SKIP();
  }

  if (device_access_denied()) {
    if (test_admin_functions_) {
      FAIL();
    } else {
      GTEST_SKIP();
    }
    __UNREACHABLE;
  }

  EnumerateDevices();
}

void AdminTest::TearDown() {
  // All pending position notifications should have returned by now, no need to wait for them.
  ring_buffer_.Unbind();

  TestBase::TearDown();
}

// For the channelization and sample_format that we've set, determine the size of each frame.
// This method assumes that SetFormat has already been sent to the driver.
void AdminTest::CalculateFrameSize() {
  if (!format_is_set_) {
    return;
  }
  EXPECT_LE(pcm_format_.valid_bits_per_sample, pcm_format_.bytes_per_sample * 8);
  frame_size_ = pcm_format_.number_of_channels * pcm_format_.bytes_per_sample;
}

void AdminTest::SelectFirstFormat() {
  if (received_get_formats()) {
    ASSERT_NE(pcm_formats().size(), 0u);

    auto& first_format = pcm_formats()[0];
    pcm_format_.number_of_channels = first_format.number_of_channels[0];
    pcm_format_.channels_to_use_bitmask = (1 << pcm_format_.number_of_channels) - 1;  // Use all.
    pcm_format_.sample_format = first_format.sample_formats[0];
    pcm_format_.bytes_per_sample = first_format.bytes_per_sample[0];
    pcm_format_.valid_bits_per_sample = first_format.valid_bits_per_sample[0];
    pcm_format_.frame_rate = first_format.frame_rates[0];
  }
}

void AdminTest::SelectLastFormat() {
  if (received_get_formats()) {
    ASSERT_NE(pcm_formats().size(), 0u);

    auto& last_format = pcm_formats()[pcm_formats().size() - 1];
    pcm_format_.number_of_channels =
        last_format.number_of_channels[last_format.number_of_channels.size() - 1];
    pcm_format_.channels_to_use_bitmask = (1 << pcm_format_.number_of_channels) - 1;  // Use all.
    pcm_format_.sample_format = last_format.sample_formats[last_format.sample_formats.size() - 1];
    pcm_format_.bytes_per_sample =
        last_format.bytes_per_sample[last_format.bytes_per_sample.size() - 1];
    pcm_format_.valid_bits_per_sample =
        last_format.valid_bits_per_sample[last_format.valid_bits_per_sample.size() - 1];
    pcm_format_.frame_rate = last_format.frame_rates[last_format.frame_rates.size() - 1];
  }
}

void AdminTest::RequestRingBuffer() {
  if (device_access_denied()) {
    return;
  }

  fuchsia::hardware::audio::Format format = {};
  format.set_pcm_format(pcm_format_);

  fidl::InterfaceHandle<fuchsia::hardware::audio::RingBuffer> ring_buffer_handle;
  stream_config()->CreateRingBuffer(std::move(format), ring_buffer_handle.NewRequest());

  zx::channel channel = ring_buffer_handle.TakeChannel();
  ring_buffer_ =
      fidl::InterfaceHandle<fuchsia::hardware::audio::RingBuffer>(std::move(channel)).Bind();
  if (!stream_config().is_bound()) {
    set_device_access_denied();
    ADD_FAILURE() << "Failed to get ring buffer channel";
    return;
  }

  ring_buffer_.set_error_handler([this](zx_status_t status) {
    if (status == ZX_ERR_PEER_CLOSED) {
      set_device_access_denied();
      ADD_FAILURE() << "Ring buffer error " << status
                    << ". Is another client already connected to the RingBuffer interface?";
    } else {
      ADD_FAILURE() << "Ring buffer error " << status;
    }
  });

  format_is_set_ = true;
  ring_buffer_ready_ = true;
}

// Request that driver set format to the lowest rate/channelization of the first range reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AdminTest::UseMinFormat() {
  ASSERT_TRUE(received_get_formats());
  ASSERT_GT(pcm_formats().size(), 0u);

  SelectFirstFormat();
  RequestRingBuffer();
  CalculateFrameSize();
}

// Request that driver set format to the highest rate/channelization of the final range reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AdminTest::UseMaxFormat() {
  ASSERT_TRUE(received_get_formats());
  ASSERT_GT(pcm_formats().size(), 0u);

  SelectLastFormat();
  RequestRingBuffer();
  CalculateFrameSize();
}

// Ring-buffer channel requests
//
// Request that the driver return the FIFO depth (in bytes), at the currently set format.
// This method relies on the ring buffer channel.
void AdminTest::RequestRingBufferProperties() {
  if (device_access_denied()) {
    return;
  }
  ASSERT_TRUE(ring_buffer_ready_);

  ring_buffer_->GetProperties([this](fuchsia::hardware::audio::RingBufferProperties prop) {
    ring_buffer_props_ = std::move(prop);

    received_get_ring_buffer_properties_ = true;
  });

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_get_ring_buffer_properties_ || device_access_denied(); });
}

// Request that the driver return a VMO handle for the ring buffer, at the currently set format.
// This method relies on the ring buffer channel.
void AdminTest::RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring) {
  if (device_access_denied()) {
    return;
  }

  min_ring_buffer_frames_ = min_ring_buffer_frames;
  notifications_per_ring_ = notifications_per_ring;
  zx::vmo ring_buffer_vmo;
  ring_buffer_->GetVmo(
      min_ring_buffer_frames, notifications_per_ring,
      [this, &ring_buffer_vmo](fuchsia::hardware::audio::RingBuffer_GetVmo_Result result) {
        EXPECT_GE(result.response().num_frames, min_ring_buffer_frames_);
        ring_buffer_frames_ = result.response().num_frames;
        ring_buffer_vmo = std::move(result.response().ring_buffer);
        EXPECT_TRUE(ring_buffer_vmo.is_valid());
        received_get_buffer_ = true;
      });

  RunLoopUntil([this]() { return received_get_buffer_ || device_access_denied(); });

  const zx_vm_option_t option_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  EXPECT_EQ(ring_buffer_mapper_.CreateAndMap(ring_buffer_frames_ * frame_size_, option_flags,
                                             nullptr, &ring_buffer_vmo,
                                             ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER),
            ZX_OK);

  FX_LOGS(DEBUG) << "Mapping size: " << ring_buffer_frames_ * frame_size_;
}

void AdminTest::SetPositionNotification() {
  if (device_access_denied()) {
    return;
  }
  watch_for_next_position_notification_ = true;

  ring_buffer_->WatchClockRecoveryPositionInfo(
      [this](fuchsia::hardware::audio::RingBufferPositionInfo position_info) {
        // If, after being dispatched, we were cancelled, then leave now.
        if (!watch_for_next_position_notification_) {
          return;
        }

        EXPECT_GT(notifications_per_ring_, 0u);

        auto now = zx::clock::get_monotonic().get();
        EXPECT_LT(start_time_, now);
        EXPECT_LT(position_info.timestamp, now);

        if (position_notification_count_) {
          EXPECT_GT(position_info.timestamp, start_time_);
          EXPECT_GT(position_info.timestamp, position_info_.timestamp);
        } else {
          EXPECT_GE(position_info.timestamp, start_time_);
        }
        running_position_ += position_info.position;
        running_position_ -= position_info_.position;

        if (position_info.position <= position_info_.position) {
          running_position_ += (ring_buffer_frames_ * frame_size_);
        }
        position_info_.timestamp = position_info.timestamp;
        position_info_.position = position_info.position;
        EXPECT_LT(position_info_.position, ring_buffer_frames_ * frame_size_);

        ++position_notification_count_;

        FX_LOGS(DEBUG) << "Position " << position_info_.position << ", notification_count "
                       << position_notification_count_ << ", running_position "
                       << running_position_;

        SetPositionNotification();
      });
}

// Set a position notification that, if called, automatically failed. We use this when we expect to
// NOT receive any position notifications. Although calling WatchClockRecoveryPositionInfo should
// clear any previously registered callback, the callback might already be dispatched to us and
// waiting, so we first clear the watch_for_next_position_notification_ flag to 'nerf' it.
void AdminTest::SetFailingPositionNotification() {
  if (device_access_denied()) {
    return;
  }
  // Disable any last pending notification from re-queueing itself.
  watch_for_next_position_notification_ = false;

  ring_buffer_->WatchClockRecoveryPositionInfo(
      [](fuchsia::hardware::audio::RingBufferPositionInfo) { FAIL(); });
}

// As mentioned above, a previous callback might be dispatched and pending, so we 'nerf' it by
// clearing watch_for_next_position_notification_. Either way, we register a callback that does not
// register for another notification nor contribute to position calculations or notification counts.
void AdminTest::ClearPositionNotification() {
  if (device_access_denied()) {
    return;
  }
  watch_for_next_position_notification_ = false;

  ring_buffer_->WatchClockRecoveryPositionInfo(
      [](fuchsia::hardware::audio::RingBufferPositionInfo) {});
}

// Request that the driver start the ring buffer engine, responding with the start_time.
// This method assumes that the ring buffer VMO was received in a successful GetBuffer response.
void AdminTest::RequestStart() {
  if (device_access_denied()) {
    return;
  }
  ASSERT_TRUE(ring_buffer_ready_);

  auto send_time = zx::clock::get_monotonic().get();
  ring_buffer_->Start([this](int64_t start_time) {
    start_time_ = start_time;
    received_start_ = true;
  });
  RunLoopUntil([this]() { return received_start_ || device_access_denied(); });
  EXPECT_GT(start_time_, send_time);
}

// Request that the driver stop the ring buffer engine, including quieting position notifications.
// This method assumes that the ring buffer engine has previously been successfully started.
void AdminTest::RequestStop() {
  if (device_access_denied()) {
    return;
  }
  ASSERT_TRUE(received_start_);

  ring_buffer_->Stop([this]() { received_stop_ = true; });
  RunLoopUntil([this]() { return received_stop_ || device_access_denied(); });
}

// Within the next position notification, Stop and fail on any subsequent position notification.
void AdminTest::RequestStopAndExpectNoPositionNotifications() {
  if (device_access_denied()) {
    return;
  }
  ASSERT_TRUE(received_start_);

  // Disable any last pending notification from re-queueing itself.
  watch_for_next_position_notification_ = false;
  ring_buffer_->WatchClockRecoveryPositionInfo(
      [this](fuchsia::hardware::audio::RingBufferPositionInfo) {
        ring_buffer_->Stop([this]() { received_stop_ = true; });

        SetFailingPositionNotification();
      });
  RunLoopUntil([this]() { return received_stop_ || device_access_denied(); });

  // We should NOT receive further position notifications. Wait a bit to give them a chance to run.
  zx::nanosleep(zx::deadline_after(zx::sec(1)));
  RunLoopUntilIdle();
}

// Wait for the specified number of position notifications.
void AdminTest::ExpectPositionNotifyCount(uint32_t count) {
  if (device_access_denied()) {
    return;
  }
  RunLoopUntil(
      [this, count]() { return position_notification_count_ >= count || device_access_denied(); });
  ClearPositionNotification();

  auto timestamp_duration = position_info_.timestamp - start_time_;
  auto observed_duration = zx::clock::get_monotonic().get() - start_time_;

  ASSERT_GT(position_notification_count_, 0u) << "No position notifications received";
  ASSERT_GE(position_notification_count_, count) << "Too few position notifications received";

  // What timestamp do we expect, for the final notification received? We know how many
  // notifications we've received; we'll multiply this by the per-notification time duration.
  // However, upon enabling notifications, our first notification might arrive immediately. Thus,
  // the average number of notification periods elapsed is (position_notification_count_ - 0.5).
  ASSERT_NE(pcm_format_.frame_rate * notifications_per_ring_, 0u);
  auto ns_per_notification =
      (zx::sec(1) * ring_buffer_frames_) / (pcm_format_.frame_rate * notifications_per_ring_);
  double average_num_notif_periods_elapsed = position_notification_count_ - 0.5;

  // Furthermore, notification timing requirements for drivers are somewhat loose, so we include a
  // tolerance range of +/- 0.75 notification periods.
  auto min_allowed_time = ns_per_notification.get() * (average_num_notif_periods_elapsed - 0.75);
  auto expected_time = ns_per_notification.get() * average_num_notif_periods_elapsed;
  auto max_allowed_time = ns_per_notification.get() * (average_num_notif_periods_elapsed + 0.75);

  FX_LOGS(DEBUG) << "Timestamp delta from min/ideal/max: " << std::setw(10)
                 << (min_allowed_time - timestamp_duration) << " : " << std::setw(10)
                 << (expected_time - timestamp_duration) << " : " << std::setw(10)
                 << (max_allowed_time - timestamp_duration);
  EXPECT_GE(timestamp_duration, min_allowed_time)
      << "Notification rate too high. Device clock rate too fast?";
  EXPECT_LE(timestamp_duration, max_allowed_time)
      << "Notification rate too low. Device clock rate too slow?";
  FX_LOGS(DEBUG) << "Expected " << average_num_notif_periods_elapsed << " intervals, was "
                 << static_cast<double>(timestamp_duration) / ns_per_notification.get();

  // Also validate when the notification was actually received (not just the timestamp).
  FX_LOGS(DEBUG) << "Observed delta from min/ideal/max : " << std::setw(10)
                 << (min_allowed_time - observed_duration) << " : " << std::setw(10)
                 << (expected_time - observed_duration) << " : " << std::setw(10)
                 << (max_allowed_time - observed_duration);
  EXPECT_GT(observed_duration, min_allowed_time);

  uint64_t expected_bytes = (timestamp_duration * pcm_format_.frame_rate * frame_size_) / ZX_SEC(1);
  FX_LOGS(DEBUG) << "Running byte position: expected " << expected_bytes << ", actual "
                 << running_position_ << ", "
                 << static_cast<double>(expected_bytes) / running_position_;
}

//
// Test cases that target each of the various admin commands
//
// Verify SET_FORMAT response (low-bit-rate) and that valid ring buffer channel is received.
TEST_P(AdminTest, SetFormatMin) {
  RequestFormats();
  UseMinFormat();
}

// Verify SET_FORMAT response (high-bit-rate) and that valid ring buffer channel is received.
TEST_P(AdminTest, SetFormatMax) {
  RequestFormats();
  UseMaxFormat();
}

// Ring Buffer channel commands
//
// Verify a valid ring buffer properties response is successfully received.
TEST_P(AdminTest, GetRingBufferProperties) {
  RequestFormats();
  UseMaxFormat();
  RequestRingBufferProperties();
}

// Verify a get buffer esponse and ring buffer VMO is successfully received.
TEST_P(AdminTest, GetBuffer) {
  RequestFormats();
  UseMinFormat();
  RequestBuffer(100, 1);
}

// Verify that a valid start response is successfully received.
TEST_P(AdminTest, Start) {
  RequestFormats();
  UseMinFormat();
  RequestBuffer(32000, 0);

  RequestStart();
}

// Verify that a valid stop command is successfully issued.
TEST_P(AdminTest, Stop) {
  RequestFormats();
  UseMinFormat();
  RequestBuffer(100, 0);

  RequestStart();
  RequestStop();
}

// Verify position notifications at fast rate (~180/sec) over approx 100 ms.
TEST_P(AdminTest, PositionNotifyFast) {
  RequestFormats();
  UseMaxFormat();
  RequestBuffer(8000, 32);

  SetPositionNotification();
  RequestStart();
  ExpectPositionNotifyCount(16);
}

// Verify position notifications at slow rate (2/sec) over approx 1 second.
TEST_P(AdminTest, PositionNotifySlow) {
  RequestFormats();
  UseMinFormat();
  RequestBuffer(48000, 2);

  SetPositionNotification();
  RequestStart();
  ExpectPositionNotifyCount(3);
}

// Verify that no position notifications arrive if notifications_per_ring is 0.
TEST_P(AdminTest, PositionNotifyNone) {
  RequestFormats();
  UseMaxFormat();
  RequestBuffer(8000, 0);

  SetFailingPositionNotification();
  RequestStart();

  // We should NOT receive any position notifications. Wait a bit to give them a chance to run.
  zx::nanosleep(zx::deadline_after(zx::msec(250)));
  RunLoopUntilIdle();
}

// Verify that no position notifications arrive after stop.
TEST_P(AdminTest, NoPositionNotifyAfterStop) {
  RequestFormats();
  UseMaxFormat();
  RequestBuffer(8000, 32);

  SetPositionNotification();
  RequestStart();
  ExpectPositionNotifyCount(3);

  // After Stop is called, no position notification should be received.
  // To validate this without any race windows: from within the next position notification itself,
  // we call Stop and register a position notification that FAILs if called.
  RequestStopAndExpectNoPositionNotifications();
}

INSTANTIATE_TEST_SUITE_P(AudioDriverTests, AdminTest,
                         testing::Values(DeviceType::Input, DeviceType::Output),
                         TestBase::DeviceTypeToString);

}  // namespace media::audio::drivers::test
