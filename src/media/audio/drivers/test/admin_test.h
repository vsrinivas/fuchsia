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

  // Set flag so position notifications (even already-enqueued ones!) cause failures.
  void FailOnPositionNotifications() { fail_on_position_notification_ = true; }
  // Clear flag so position notifications (even already-enqueued ones) do not cause failures.
  void AllowPositionNotifications() { fail_on_position_notification_ = false; }
  void PositionNotificationCallback(fuchsia::hardware::audio::RingBufferPositionInfo position_info);

  fidl::InterfacePtr<fuchsia::hardware::audio::RingBuffer>& ring_buffer() { return ring_buffer_; }
  uint32_t ring_buffer_frames() const { return ring_buffer_frames_; }
  fuchsia::hardware::audio::PcmFormat pcm_format() const { return pcm_format_; }

  uint32_t notifications_per_ring() const { return notifications_per_ring_; }
  const zx::time& start_time() const { return start_time_; }
  uint16_t frame_size() const { return frame_size_; }

 private:
  fidl::InterfacePtr<fuchsia::hardware::audio::RingBuffer> ring_buffer_;
  fuchsia::hardware::audio::RingBufferProperties ring_buffer_props_;

  uint32_t min_ring_buffer_frames_ = 0;
  uint32_t notifications_per_ring_ = 0;
  uint32_t ring_buffer_frames_ = 0;
  fzl::VmoMapper ring_buffer_mapper_;

  zx::time start_time_;
  fuchsia::hardware::audio::PcmFormat pcm_format_;
  uint16_t frame_size_ = 0;

  // Position notifications are hanging-gets. On receipt, should we register the next one? Or fail?
  bool fail_on_position_notification_ = false;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_
