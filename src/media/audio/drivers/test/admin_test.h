// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_

#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/time.h>
#include <zircon/device/audio.h>
#include <zircon/errors.h>

#include "src/media/audio/drivers/test/test_base.h"
#include "src/media/audio/lib/test/message_transceiver.h"

namespace media::audio::drivers::test {

class AdminTest : public TestBase {
 public:
  explicit AdminTest(const DeviceEntry& dev_entry) : TestBase(dev_entry) {}

 protected:
  void RequestMinFormat();
  void RequestMaxFormat();

  void CalculateFrameSize();

  void RequestRingBufferChannel();
  void RequestRingBufferProperties();
  void RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring);
  void ActivateChannels(uint64_t active_channels_bitmask);

  void RequestStart();
  void RequestStartAndExpectDisconnect(zx_status_t expected_error);

  void RequestStop();
  void RequestStopAndExpectNoPositionNotifications();
  void RequestStopAndExpectDisconnect(zx_status_t expected_error);

  // Request a position notification that will record timestamp/position and register for another.
  void EnablePositionNotifications();
  // Clear flag so that any pending position notification will not request yet another.
  void DisablePositionNotifications() { request_next_position_notification_ = false; }
  // Set flag so position notifications (even already-enqueued ones!) cause failures.
  void FailOnPositionNotifications() { fail_on_position_notification_ = true; }
  // Clear flag so position notifications (even already-enqueued ones) do not cause failures.
  void AllowPositionNotifications() { fail_on_position_notification_ = false; }

  void RequestPositionNotification();
  void PositionNotificationCallback(fuchsia::hardware::audio::RingBufferPositionInfo position_info);
  void ExpectPositionNotifyCount(uint32_t count);
  void ValidatePositionInfo();

  fidl::InterfacePtr<fuchsia::hardware::audio::RingBuffer>& ring_buffer() { return ring_buffer_; }
  uint32_t ring_buffer_frames() const { return ring_buffer_frames_; }
  fuchsia::hardware::audio::PcmFormat pcm_format() const { return pcm_format_; }

 private:
  fidl::InterfacePtr<fuchsia::hardware::audio::RingBuffer> ring_buffer_;
  fuchsia::hardware::audio::RingBufferProperties ring_buffer_props_;
  fuchsia::hardware::audio::RingBufferPositionInfo saved_position_ = {};

  uint32_t min_ring_buffer_frames_ = 0;
  uint32_t notifications_per_ring_ = 0;
  uint32_t ring_buffer_frames_ = 0;
  fzl::VmoMapper ring_buffer_mapper_;

  zx::time start_time_;
  fuchsia::hardware::audio::PcmFormat pcm_format_;
  uint16_t frame_size_ = 0;

  // Position notifications are hanging-gets. On receipt, should we register the next one? Or fail?
  bool request_next_position_notification_ = false;
  bool record_position_info_ = false;
  bool fail_on_position_notification_ = false;
  uint32_t position_notification_count_ = 0;
  uint64_t running_position_ = 0;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_
