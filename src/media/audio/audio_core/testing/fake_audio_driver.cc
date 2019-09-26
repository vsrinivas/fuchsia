// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/fake_audio_driver.h"

#include <gtest/gtest.h>

namespace media::audio::testing {

FakeAudioDriver::FakeAudioDriver(zx::channel channel) {
  zx_status_t status = stream_transceiver_.Init(
      std::move(channel), fit::bind_member(this, &FakeAudioDriver::OnInboundStreamMessage),
      fit::bind_member(this, &FakeAudioDriver::OnInboundStreamError));
  FXL_CHECK(status == ZX_OK);
}

void FakeAudioDriver::OnInboundStreamError(zx_status_t status) {}

void FakeAudioDriver::OnInboundStreamMessage(test::MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  switch (header.cmd) {
    case AUDIO_STREAM_CMD_GET_FORMATS:
      HandleCommandGetFormats(message.BytesAs<audio_stream_cmd_get_formats_req_t>());
      break;
    case AUDIO_STREAM_CMD_SET_FORMAT:
      HandleCommandSetFormat(message.BytesAs<audio_stream_cmd_set_format_req_t>());
      break;
    case AUDIO_STREAM_CMD_GET_GAIN:
      HandleCommandGetGain(message.BytesAs<audio_stream_cmd_get_gain_req_t>());
      break;
    case AUDIO_STREAM_CMD_GET_UNIQUE_ID:
      HandleCommandGetUniqueId(message.BytesAs<audio_stream_cmd_get_unique_id_req_t>());
      break;
    case AUDIO_STREAM_CMD_GET_STRING:
      HandleCommandGetString(message.BytesAs<audio_stream_cmd_get_string_req_t>());
      break;
    default:
      EXPECT_TRUE(false) << "Unrecognized header.cmd value " << header.cmd;
      break;
  }
}

void FakeAudioDriver::HandleCommandGetUniqueId(
    const audio_stream_cmd_get_unique_id_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_unique_id_resp>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  std::memcpy(response.unique_id.data, uid_.data, sizeof(uid_.data));
  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandGetString(const audio_stream_cmd_get_string_req_t& request) {
  std::string_view response_string;

  switch (request.id) {
    case AUDIO_STREAM_STR_ID_MANUFACTURER:
      response_string = manufacturer_;
      break;
    case AUDIO_STREAM_STR_ID_PRODUCT:
      response_string = product_;
      break;
    default:
      EXPECT_TRUE(false) << "Unrecognized string id " << request.id;
      return;
  }

  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_string_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.id = request.id;
  response.strlen = response_string.size();
  EXPECT_TRUE(response.strlen < sizeof(response.str));
  response_string.copy(reinterpret_cast<char*>(&response.str[0]), response.strlen);
  response.str[response.strlen] = '\0';
  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandGetGain(const audio_stream_cmd_get_gain_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_gain_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.cur_mute = cur_mute_;
  response.cur_agc = cur_agc_;
  response.cur_gain = cur_gain_;
  response.can_mute = can_mute_;
  response.can_agc = can_agc_;
  response.min_gain = gain_limits_.first;
  response.max_gain = gain_limits_.second;
  response.gain_step = 0.001f;
  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandGetFormats(const audio_stream_cmd_get_formats_req_t& request) {
  // Multiple reponses isn't implemented yet.
  FXL_CHECK(formats_.size() <= AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);

  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_formats_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.format_range_count = formats_.size();
  response.first_format_range_ndx = 0;
  size_t idx = 0;
  for (const auto& format : formats_) {
    response.format_ranges[idx++] = format;
  }
  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandSetFormat(const audio_stream_cmd_set_format_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_set_format_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.result = ZX_OK;
  response.external_delay_nsec = 0;

  // Note: Upon success, a channel used to control the audio buffer will also be returned.
  zx::channel local_channel;
  zx::channel remote_channel;
  zx_status_t status = zx::channel::create(0u, &local_channel, &remote_channel);
  EXPECT_EQ(ZX_OK, status);

  status = ring_buffer_transceiver_.Init(
      std::move(local_channel),
      fit::bind_member(this, &FakeAudioDriver::OnInboundRingBufferMessage),
      fit::bind_member(this, &FakeAudioDriver::OnInboundRingBufferError));
  EXPECT_EQ(ZX_OK, status);

  response_message.handles_.clear();
  response_message.handles_.push_back(std::move(remote_channel.get()));

  status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::OnInboundRingBufferMessage(test::MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  switch (header.cmd) {
    case AUDIO_RB_CMD_GET_FIFO_DEPTH:
      HandleCommandGetFifoDepth(message.BytesAs<audio_rb_cmd_get_fifo_depth_req_t>());
      break;
    case AUDIO_RB_CMD_GET_BUFFER:
      HandleCommandGetBuffer(message.BytesAs<audio_rb_cmd_get_buffer_req_t>());
      break;
    case AUDIO_RB_CMD_START:
      HandleCommandStart(message.BytesAs<audio_rb_cmd_start_req_t>());
      break;
    case AUDIO_RB_CMD_STOP:
      HandleCommandStop(message.BytesAs<audio_rb_cmd_stop_req_t>());
      break;
    default:
      EXPECT_TRUE(false) << "Unrecognized header.cmd value " << header.cmd;
      break;
  }
}

void FakeAudioDriver::OnInboundRingBufferError(zx_status_t status) {}

void FakeAudioDriver::HandleCommandGetFifoDepth(audio_rb_cmd_get_fifo_depth_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_rb_cmd_get_fifo_depth_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.result = ZX_OK;
  response.fifo_depth = 0;

  zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandGetBuffer(audio_rb_cmd_get_buffer_req_t& request) {}

void FakeAudioDriver::HandleCommandStart(audio_rb_cmd_start_req_t& request) {
  ASSERT_TRUE(false) << "Unexpected START command received";
}

void FakeAudioDriver::HandleCommandStop(audio_rb_cmd_stop_req_t& request) {
  ASSERT_TRUE(false) << "Unexpected STOP command received";
}

}  // namespace media::audio::testing
