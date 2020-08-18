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
  static void SetUpTestSuite();

 protected:
  void SetUp() override;
  void TearDown() override;

  void set_device_access_denied() { device_access_denied_[device_type()] = true; }
  bool device_access_denied() const { return device_access_denied_[device_type()]; }

  void SelectFirstFormat();
  void SelectLastFormat();
  void UseMinFormat();
  void UseMaxFormat();

  void CalculateFrameSize();

  // The SET_FORMAT command response is followed by conveyance of the ring-buffer channel, which is
  // used for the remaining test cases.
  void ExtractRingBufferChannel(
      media::audio::test::MessageTransceiver::Message set_format_response);

  void RequestRingBuffer();
  void RequestRingBufferProperties();
  void RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring);

  // Register for a position notification; in each notification handler, we increment counts,
  // validate the timestamp and position info received, and register for the next notification.
  void SetPositionNotification();
  // Set a position notification handler that automatically FAILs if invoked. This is used when we
  // definitely should not receive any position notifications.
  void SetFailingPositionNotification();
  // Register a position notification that does NOT in turn register for the next one. Used to break
  // the ongoing chain of position notification callbacks, once a testcase is complete.
  void ClearPositionNotification();

  void RequestStart();
  void RequestStop();
  //
  void RequestStopAndExpectNoPositionNotifications();

  void ExpectPositionNotifyCount(uint32_t count);
  void ExpectNoPositionNotifications();

 private:
  // for DeviceType::Input and DeviceType::Output
  static bool device_access_denied_[2];

  bool ring_buffer_ready_ = false;
  fidl::InterfacePtr<fuchsia::hardware::audio::RingBuffer> ring_buffer_;
  fuchsia::hardware::audio::RingBufferProperties ring_buffer_props_;
  fuchsia::hardware::audio::RingBufferPositionInfo position_info_ = {};

  uint32_t min_ring_buffer_frames_ = 0;
  uint32_t notifications_per_ring_ = 0;
  uint32_t ring_buffer_frames_ = 0;
  fzl::VmoMapper ring_buffer_mapper_;

  zx_time_t start_time_ = 0;
  bool received_get_ring_buffer_properties_ = false;
  bool received_get_buffer_ = false;
  bool received_start_ = false;
  bool received_stop_ = false;
  fuchsia::hardware::audio::PcmFormat pcm_format_;
  bool format_is_set_ = false;
  uint16_t frame_size_ = 0;

  // Position notifications are single hanging-get notifications. This bool indicates whether our
  // notification handler should automatically register for the next one.
  bool watch_for_next_position_notification_ = false;
  uint32_t position_notification_count_ = 0;
  uint64_t running_position_ = 0;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_
