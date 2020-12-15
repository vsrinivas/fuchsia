// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/admin_test.h"

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>
#include <zircon/compiler.h>

#include <algorithm>
#include <cstring>

#include "lib/zx/time.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::drivers::test {

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
  ASSERT_NE(pcm_formats().size(), 0u);

  auto& first_format = pcm_formats()[0];
  pcm_format_.number_of_channels = first_format.number_of_channels[0];
  pcm_format_.channels_to_use_bitmask = (1 << pcm_format_.number_of_channels) - 1;  // Use all.
  pcm_format_.sample_format = first_format.sample_formats[0];
  pcm_format_.bytes_per_sample = first_format.bytes_per_sample[0];
  pcm_format_.valid_bits_per_sample = first_format.valid_bits_per_sample[0];
  pcm_format_.frame_rate = first_format.frame_rates[0];
}

void AdminTest::SelectLastFormat() {
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

void AdminTest::RequestRingBufferChannel() {
  fuchsia::hardware::audio::Format format = {};
  format.set_pcm_format(pcm_format_);

  fidl::InterfaceHandle<fuchsia::hardware::audio::RingBuffer> ring_buffer_handle;
  stream_config()->CreateRingBuffer(std::move(format), ring_buffer_handle.NewRequest());

  zx::channel channel = ring_buffer_handle.TakeChannel();
  ring_buffer_ =
      fidl::InterfaceHandle<fuchsia::hardware::audio::RingBuffer>(std::move(channel)).Bind();
  ring_buffer_.set_error_handler([this](zx_status_t status) {
    set_failed();
    if (status == ZX_ERR_PEER_CLOSED) {
      FAIL() << "RingBuffer channel error " << status
             << ": is another client already connected to the RingBuffer interface?";
    }
    FAIL() << "RingBuffer channel error " << status;
  });

  if (!stream_config().is_bound() || !ring_buffer_.is_bound()) {
    FAIL() << "Failed to get ring buffer channel";
  }

  format_is_set_ = true;
  ring_buffer_ready_ = true;
}

// Request that driver set format to the lowest rate/channelization of the first range reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AdminTest::RequestMinFormat() {
  ASSERT_TRUE(received_get_formats());
  ASSERT_GT(pcm_formats().size(), 0u);

  SelectFirstFormat();
  RequestRingBufferChannel();
  CalculateFrameSize();
}

// Request that driver set the highest rate/channelization of the final range reported. This method
// assumes that the driver has already successfully responded to a GetFormats request.
void AdminTest::RequestMaxFormat() {
  ASSERT_TRUE(received_get_formats());
  ASSERT_GT(pcm_formats().size(), 0u);

  SelectLastFormat();
  RequestRingBufferChannel();
  CalculateFrameSize();
}

// Ring-buffer channel requests
//
// Request the FIFO depth in bytes, at the current format (relies on the ring buffer channel).
void AdminTest::RequestRingBufferProperties() {
  ASSERT_TRUE(ring_buffer_ready_);

  bool received_get_ring_buffer_properties = false;
  ring_buffer_->GetProperties([this, &received_get_ring_buffer_properties](
                                  fuchsia::hardware::audio::RingBufferProperties prop) {
    ring_buffer_props_ = std::move(prop);

    received_get_ring_buffer_properties = true;
  });

  // ring_buffer_->GetProperties can return an error, so we check for failed as well
  RunLoopUntil([this, &received_get_ring_buffer_properties]() {
    return received_get_ring_buffer_properties || failed();
  });
}

// Request the ring buffer's VMO handle, at the current format (relies on the ring buffer channel).
void AdminTest::RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring) {
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

  RunLoopUntil([this]() { return received_get_buffer_ || failed(); });
  if (failed()) {
    return;
  }

  ring_buffer_mapper_.Unmap();
  const zx_vm_option_t option_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  EXPECT_EQ(ring_buffer_mapper_.CreateAndMap(ring_buffer_frames_ * frame_size_, option_flags,
                                             nullptr, &ring_buffer_vmo,
                                             ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER),
            ZX_OK);
}

void AdminTest::SetPositionNotification() {
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

        SetPositionNotification();
      });
}

// Set a position notification that, if called, automatically failed. We use this when we expect to
// NOT receive any position notifications. Although calling WatchClockRecoveryPositionInfo should
// clear any previously registered callback, the callback might already be dispatched to us and
// waiting, so we first clear the watch_for_next_position_notification_ flag to 'nerf' it.
void AdminTest::SetFailingPositionNotification() {
  // Disable any last pending notification from re-queueing itself.
  watch_for_next_position_notification_ = false;

  ring_buffer_->WatchClockRecoveryPositionInfo(
      [](fuchsia::hardware::audio::RingBufferPositionInfo) {
        FAIL() << "Unexpected position notification received";
      });
}

// As mentioned above, a previous callback might be dispatched and pending, so we 'nerf' it by
// clearing watch_for_next_position_notification_. Either way, we register a callback that does not
// register for another notification nor contribute to position calculations or notification counts.
void AdminTest::ClearPositionNotification() {
  watch_for_next_position_notification_ = false;

  ring_buffer_->WatchClockRecoveryPositionInfo(
      [](fuchsia::hardware::audio::RingBufferPositionInfo) {});
}

// Request that the driver start the ring buffer engine, responding with the start_time.
// This method assumes that the ring buffer VMO was received in a successful GetBuffer response.
void AdminTest::RequestStart() {
  ASSERT_TRUE(ring_buffer_ready_);

  auto send_time = zx::clock::get_monotonic().get();
  ring_buffer_->Start([this](int64_t start_time) {
    start_time_ = start_time;
    received_start_ = true;
  });
  RunLoopUntil([this]() { return received_start_ || failed(); });
  if (failed()) {
    return;
  }

  EXPECT_GT(start_time_, send_time);
}

// Request that driver stop the ring buffer, including quieting position notifications.
// This method assumes that the ring buffer engine has previously been successfully started.
void AdminTest::RequestStop() {
  if (failed()) {
    return;
  }
  ASSERT_TRUE(received_start_);

  ring_buffer_->Stop([this]() { received_stop_ = true; });
  RunLoopUntil([this]() { return received_stop_ || failed(); });
}

// After Stop is called, no position notification should be received.
// To validate this without any race windows: from within the next position notification itself,
// we call Stop and register a position notification that FAILs if called.
// Within the next position notification, Stop and fail on any subsequent position notification.
void AdminTest::RequestStopAndExpectNoPositionNotifications() {
  if (failed()) {
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

  RunLoopUntil([this]() { return received_stop_ || failed(); });

  // We should NOT receive further position notifications. If we do, it triggers an error.
}

// Wait for the specified number of position notifications.
void AdminTest::ExpectPositionNotifyCount(uint32_t count) {
  RunLoopUntil([this, count]() { return position_notification_count_ >= count || failed(); });
  if (failed()) {
    return;
  }

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

// Test cases that target each of the various admin commands

// Verify valid responses: ring buffer properties, get buffer, ring buffer VMO.
DEFINE_ADMIN_TEST_CLASS(GetRingBufferProperties, {
  RequestFormats();
  RequestMaxFormat();
  RequestRingBufferProperties();
});
DEFINE_ADMIN_TEST_CLASS(GetBuffer, {
  RequestFormats();
  RequestMinFormat();
  RequestBuffer(100, 1);
});

// Verify that valid start and stop responses are successfully received.
DEFINE_ADMIN_TEST_CLASS(Start, {
  RequestFormats();
  RequestMinFormat();
  RequestBuffer(32000, 0);
  RequestStart();
});
DEFINE_ADMIN_TEST_CLASS(Stop, {
  RequestFormats();
  RequestMinFormat();
  RequestBuffer(100, 0);
  RequestStart();
  RequestStop();
  WaitForError();
});

// Verify position notifications at fast (~180/sec) and slow (2/sec) rate.
DEFINE_ADMIN_TEST_CLASS(PositionNotifyFast, {
  RequestFormats();
  RequestMaxFormat();
  RequestBuffer(8000, 32);
  SetPositionNotification();
  RequestStart();
  ExpectPositionNotifyCount(16);
  WaitForError();
});
// Notifications arrive every 500 msec; we must wait longer than the default 100 msec.
DEFINE_ADMIN_TEST_CLASS(PositionNotifySlow, {
  RequestFormats();
  RequestMinFormat();
  RequestBuffer(48000, 2);
  SetPositionNotification();
  RequestStart();
  ExpectPositionNotifyCount(3);
  WaitForError(zx::msec(600));
});

// Verify no position notifications arrive after stop, or if notifications_per_ring is 0.
DEFINE_ADMIN_TEST_CLASS(NoPositionNotifyAfterStop, {
  RequestFormats();
  RequestMaxFormat();
  RequestBuffer(8000, 32);
  SetPositionNotification();
  RequestStart();
  ExpectPositionNotifyCount(3);
  RequestStopAndExpectNoPositionNotifications();
  WaitForError();
});
DEFINE_ADMIN_TEST_CLASS(PositionNotifyNone, {
  RequestFormats();
  RequestMaxFormat();
  RequestBuffer(8000, 0);
  SetFailingPositionNotification();
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
  if (!expect_audio_core_connected || device_entry.dir_fd == DeviceEntry::kA2dp) {
    REGISTER_ADMIN_TEST(GetRingBufferProperties, device_entry);
    REGISTER_ADMIN_TEST(GetBuffer, device_entry);
    REGISTER_ADMIN_TEST(Start, device_entry);
    REGISTER_ADMIN_TEST(Stop, device_entry);

    // For now, the following test cases fail on a2dp-source.
    // TODO(fxbug.dev/66431): fix a2dp-source and enable these test cases for all devices.
    if (device_entry.dir_fd != DeviceEntry::kA2dp) {
      REGISTER_ADMIN_TEST(PositionNotifyFast, device_entry);
      REGISTER_ADMIN_TEST(PositionNotifySlow, device_entry);
      REGISTER_ADMIN_TEST(NoPositionNotifyAfterStop, device_entry);
    }

    REGISTER_ADMIN_TEST(PositionNotifyNone, device_entry);
  }
}

}  // namespace media::audio::drivers::test
