// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_

#include <lib/fzl/vmo-mapper.h>
#include <zircon/device/audio.h>

#include "src/media/audio/drivers/test/basic_test.h"
#include "src/media/audio/lib/test/message_transceiver.h"

namespace media::audio::drivers::test {

class AdminTest : public TestBase {
 public:
  explicit AdminTest(const DeviceEntry& dev_entry) : TestBase(dev_entry) {}

 protected:
  void SelectFirstFormat();
  void SelectLastFormat();
  void RequestMinFormat();
  void RequestMaxFormat();

  void CalculateFrameSize();

  void RequestRingBufferChannel();
  void RequestRingBufferProperties();
  void RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring);

  // Register a position notification to validate timestamp/position and register for the next one.
  void SetPositionNotification();
  // Register a position notification that FAILs if we receive one.
  void SetFailingPositionNotification();
  // Register a notification that DOESN'T register for the next one (breaks the chain at test-end).
  void ClearPositionNotification();

  void RequestStart();
  void RequestStop();
  void ExpectPositionNotifyCount(uint32_t count);
  void RequestStopAndExpectNoPositionNotifications();

 private:
  bool ring_buffer_ready_ = false;
  fidl::InterfacePtr<fuchsia::hardware::audio::RingBuffer> ring_buffer_;
  fuchsia::hardware::audio::RingBufferProperties ring_buffer_props_;
  fuchsia::hardware::audio::RingBufferPositionInfo position_info_ = {};

  uint32_t min_ring_buffer_frames_ = 0;
  uint32_t notifications_per_ring_ = 0;
  uint32_t ring_buffer_frames_ = 0;
  fzl::VmoMapper ring_buffer_mapper_;

  zx_time_t start_time_ = 0;
  bool received_get_buffer_ = false;
  bool received_start_ = false;
  bool received_stop_ = false;
  fuchsia::hardware::audio::PcmFormat pcm_format_;
  bool format_is_set_ = false;
  uint16_t frame_size_ = 0;

  // Position notifications are hanging-gets. On receipt, should we register for the next one?
  bool watch_for_next_position_notification_ = false;
  uint32_t position_notification_count_ = 0;
  uint64_t running_position_ = 0;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_
