// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_TEST_AUDIO_DRIVER_TEST_H_
#define SRC_MEDIA_AUDIO_DRIVERS_TEST_AUDIO_DRIVER_TEST_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <zircon/device/audio.h>

#include "src/lib/fsl/io/device_watcher.h"
#include "src/media/audio/lib/test/message_transceiver.h"
#include "src/media/audio/lib/test/test_fixture.h"

namespace media::audio::test {

constexpr size_t kUniqueIdLength = 16;

// Except for sentinel value -1 (external clock domain), negative clock domain values are invalid
constexpr int32_t kInvalidClockDomain = -2;

enum DeviceType { Input, Output };

class AudioDriverTest : public TestFixture {
 protected:
  static void SetUpTestSuite();
  static zx_txid_t NextTransactionId();

  void SetUp() override;
  void TearDown() override;

  bool WaitForDevice(DeviceType device_type);
  void AddDevice(int dir_fd, const std::string& name, DeviceType device_type);

  void RequestUniqueId();
  void RequestManufacturerString();
  void RequestProductString();
  void RequestClockDomain();
  void RequestGain();
  void RequestSetGain();
  void RequestSetGain(audio_set_gain_flags_t flags, float gain_db);
  void RequestFormats();
  void SelectFirstFormat();
  void SelectLastFormat();
  void RequestSetFormatMin();
  void RequestSetFormatMax();
  void RequestPlugDetect();

  void RequestFifoDepth();
  void RequestBuffer(uint32_t min_ring_buffer_frames, uint32_t notifications_per_ring);
  void RequestStart();
  void RequestStop();

  void OnInboundStreamMessage(MessageTransceiver::Message message);

  bool ValidateResponseCommand(audio_cmd_hdr header, audio_cmd_t expected_command);
  void ValidateResponseTransaction(audio_cmd_hdr header, zx_txid_t expected_transaction_id);
  bool ValidateResponseHeader(audio_cmd_hdr header, zx_txid_t expected_transaction_id,
                              audio_cmd_t expected_command);

  void HandleGetUniqueIdResponse(const audio_stream_cmd_get_unique_id_resp_t& response);
  void HandleGetClockDomainResponse(const audio_stream_cmd_get_clock_domain_resp_t& response);
  void HandleGetStringResponse(const audio_stream_cmd_get_string_resp_t& response);

  void HandleGetGainResponse(const audio_stream_cmd_get_gain_resp_t& response);
  void HandleSetGainResponse(const audio_stream_cmd_set_gain_resp_t& response);

  void HandleGetFormatsResponse(const audio_stream_cmd_get_formats_resp_t& response);
  void HandleSetFormatResponse(const audio_stream_cmd_set_format_resp_t& response);
  void CalculateFrameSize();

  void HandlePlugDetect(audio_pd_notify_flags_t flags, zx_time_t plug_state_time);
  void HandlePlugDetectResponse(const audio_stream_cmd_plug_detect_resp_t& response);
  void HandlePlugDetectNotify(const audio_stream_cmd_plug_detect_resp_t& notify);

  void ExtractRingBufferChannel(MessageTransceiver::Message set_format_response);
  void OnInboundRingBufferMessage(MessageTransceiver::Message message);

  void HandleGetFifoDepthResponse(const audio_rb_cmd_get_fifo_depth_resp_t& response);
  void HandleGetBufferResponse(const audio_rb_cmd_get_buffer_resp_t& response);
  void ExtractRingBuffer(MessageTransceiver::Message get_buffer_response);

  void HandleStartResponse(const audio_rb_cmd_start_resp_t& response);
  void HandleStopResponse(const audio_rb_cmd_stop_resp_t& response);

  void HandlePositionNotify(const audio_rb_position_notify_t& notify);
  void ExpectPositionNotifyCount(uint32_t count);
  void ExpectNoPositionNotifications();

  const MessageTransceiver& stream_transceiver() { return stream_transceiver_; }
  const MessageTransceiver& ring_buffer_transceiver() { return ring_buffer_transceiver_; }

 private:
  static std::atomic_uint32_t unique_transaction_id_;

  static bool no_input_devices_found_;
  static bool no_output_devices_found_;
  std::vector<std::unique_ptr<fsl::DeviceWatcher>> watchers_;

  DeviceType device_type_;

  zx::channel stream_channel_;
  bool stream_channel_ready_ = false;
  bool ring_buffer_channel_ready_ = false;
  bool ring_buffer_ready_ = false;

  MessageTransceiver stream_transceiver_{dispatcher()};
  MessageTransceiver ring_buffer_transceiver_{dispatcher()};

  zx_txid_t unique_id_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t manufacturer_string_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t product_string_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t get_clock_domain_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t get_gain_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t get_formats_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;

  zx_txid_t set_gain_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t set_format_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t plug_detect_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;

  zx_txid_t get_fifo_depth_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t get_buffer_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t start_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t stop_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;
  zx_txid_t get_position_transaction_id_ = AUDIO_INVALID_TRANSACTION_ID;

  std::array<uint8_t, kUniqueIdLength> unique_id_;
  std::string manufacturer_;
  std::string product_;

  int32_t clock_domain_ = kInvalidClockDomain;

  bool cur_mute_ = false;
  bool can_mute_ = false;
  bool set_mute_ = false;

  bool cur_agc_ = false;
  bool can_agc_ = false;
  bool set_agc_ = false;

  float cur_gain_ = 0.0f;
  float min_gain_ = 0.0f;
  float max_gain_ = 0.0f;
  float gain_step_ = 0.0f;
  float set_gain_ = 0.0f;

  std::vector<audio_stream_format_range_t> format_ranges_;
  uint16_t get_formats_range_count_ = 0;
  uint16_t next_format_range_ndx_ = 0;

  uint64_t external_delay_nsec_ = 0;
  uint32_t frame_rate_ = 0;
  audio_sample_format_t sample_format_ = 0;
  uint16_t num_channels_ = 0;
  uint16_t frame_size_ = 0;

  bool hardwired_ = false;
  bool should_plug_notify_ = false;
  bool can_plug_notify_ = false;
  bool plugged_ = false;
  zx_time_t plug_state_time_ = 0;

  uint32_t fifo_depth_ = 0;

  uint32_t min_ring_buffer_frames_ = 0;
  uint32_t notifications_per_ring_ = 0;
  uint32_t ring_buffer_frames_ = 0;
  fzl::VmoMapper ring_buffer_;

  zx_time_t start_time_ = 0;

  uint32_t ring_buffer_position_ = 0;
  zx_time_t last_monotonic_time_ = 0;

  bool received_get_unique_id_ = false;
  bool received_get_string_manufacturer_ = false;
  bool received_get_string_product_ = false;
  bool received_get_clock_domain_ = false;
  bool received_get_gain_ = false;
  bool received_get_formats_ = false;

  bool received_set_gain_ = false;
  bool received_set_format_ = false;
  bool received_plug_detect_ = false;
  bool received_plug_detect_notify_ = false;

  bool received_get_fifo_depth_ = false;
  bool received_get_buffer_ = false;
  bool received_start_ = false;
  bool received_stop_ = false;
  uint32_t position_notification_count_ = 0;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_DRIVERS_TEST_AUDIO_DRIVER_TEST_H_
