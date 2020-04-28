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

  // SET_FORMAT (an admin command) is on the stream-config channel, so we must provide a message
  // handler. Any other stream-config command received during these tests is considered an error.
  void HandleInboundStreamMessage(media::audio::test::MessageTransceiver::Message message) override;

  void SelectFirstFormat();
  void SelectLastFormat();
  void RequestSetFormatMin();
  void RequestSetFormatMax();

  void HandleSetFormatResponse(const audio_stream_cmd_set_format_resp_t& response);
  void CalculateFrameSize();

  // The SET_FORMAT command response is followed by conveyance of the ring-buffer channel, which is
  // used for the remaining test cases.
  void ExtractRingBufferChannel(
      media::audio::test::MessageTransceiver::Message set_format_response);
  const media::audio::test::MessageTransceiver& ring_buffer_transceiver() {
    return ring_buffer_transceiver_;
  }

  void OnInboundRingBufferMessage(media::audio::test::MessageTransceiver::Message message);

  void RequestFifoDepth();
  void RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring);
  void RequestStart();
  void RequestStop();

  void HandleGetFifoDepthResponse(const audio_rb_cmd_get_fifo_depth_resp_t& response);
  void HandleGetBufferResponse(const audio_rb_cmd_get_buffer_resp_t& response);
  void ExtractRingBuffer(media::audio::test::MessageTransceiver::Message get_buffer_response);

  void HandleStartResponse(const audio_rb_cmd_start_resp_t& response);
  void HandleStopResponse(const audio_rb_cmd_stop_resp_t& response);

  void HandlePositionNotify(const audio_rb_position_notify_t& notify);
  void ExpectPositionNotifyCount(uint32_t count);
  void ExpectNoPositionNotifications();

 private:
  // for DeviceType::Input and DeviceType::Output
  static bool device_access_denied_[2];

  bool ring_buffer_channel_ready_ = false;
  bool ring_buffer_ready_ = false;

  media::audio::test::MessageTransceiver ring_buffer_transceiver_{dispatcher()};

  zx_txid_t set_format_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;

  zx_txid_t get_fifo_depth_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t get_buffer_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t start_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t stop_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t get_position_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;

  // Returned values from the commands
  // SET_FORMAT
  uint64_t external_delay_nsec_ = 0;
  uint32_t frame_rate_ = 0;
  audio_sample_format_t sample_format_ = 0;
  uint16_t num_channels_ = 0;
  uint16_t frame_size_ = 0;

  // GET_FIFO_DEPTH
  uint32_t fifo_depth_ = 0;

  // GET_RING_BUFFER
  uint32_t min_ring_buffer_frames_ = 0;
  uint32_t notifications_per_ring_ = 0;
  uint32_t ring_buffer_frames_ = 0;
  zx_duration_t ring_buffer_duration_;
  fzl::VmoMapper ring_buffer_;

  // START
  zx_time_t start_time_ = 0;

  // POSITION_NOTIFY
  uint32_t ring_buffer_position_ = 0;
  zx_time_t last_monotonic_time_ = 0;

  bool received_set_format_ = false;
  bool received_get_fifo_depth_ = false;
  bool received_get_buffer_ = false;
  bool received_start_ = false;
  bool received_stop_ = false;
  uint32_t position_notification_count_ = 0;
};

}  // namespace media::audio::drivers::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_ADMIN_TEST_H_
