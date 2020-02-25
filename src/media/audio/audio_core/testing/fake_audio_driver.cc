// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/testing/fake_audio_driver.h"

#include <audio-proto-utils/format-utils.h>
#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"

namespace media::audio::testing {

FakeAudioDriver::FakeAudioDriver(zx::channel channel, async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher),
      stream_transceiver_(dispatcher),
      ring_buffer_transceiver_(dispatcher) {
  zx_status_t status = stream_transceiver_.Init(
      std::move(channel), fit::bind_member(this, &FakeAudioDriver::OnInboundStreamMessage),
      fit::bind_member(this, &FakeAudioDriver::OnInboundStreamError));
  FX_CHECK(status == ZX_OK);
  // Initially leave the driver 'stopped' so that it won't reply to any messages until |Start| is
  // called.
  stream_transceiver_.StopProcessing();
}

void FakeAudioDriver::Start() {
  stream_transceiver_.ResumeProcessing();
  if (ring_buffer_transceiver_.channel()) {
    ring_buffer_transceiver_.ResumeProcessing();
  }
}

void FakeAudioDriver::Stop() {
  stream_transceiver_.StopProcessing();
  if (ring_buffer_transceiver_.channel()) {
    ring_buffer_transceiver_.StopProcessing();
  }
}

fzl::VmoMapper FakeAudioDriver::CreateRingBuffer(size_t size) {
  FX_CHECK(!ring_buffer_) << "Calling CreateRingBuffer multiple times is not supported";

  ring_buffer_size_ = size;
  fzl::VmoMapper mapper;
  mapper.CreateAndMap(ring_buffer_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                      &ring_buffer_);
  return mapper;
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
    case AUDIO_STREAM_CMD_PLUG_DETECT:
      HandleCommandPlugDetect(message.BytesAs<audio_stream_cmd_plug_detect_req_t>());
      break;
    case AUDIO_STREAM_CMD_GET_CLOCK_DOMAIN:
      HandleCommandGetClockDomain(message.BytesAs<audio_stream_cmd_get_clock_domain_req_t>());
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
  FX_CHECK(formats_.size() <= AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);

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
  response_message.handles_.push_back(remote_channel.release());

  status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);

  selected_format_ = {
      .frames_per_second = request.frames_per_second,
      .sample_format = request.sample_format,
      .channels = request.channels,
  };
}

void FakeAudioDriver::HandleCommandPlugDetect(const audio_stream_cmd_plug_detect_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_plug_detect_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;

  // For now we represent a hardwired device. We should make it possible to test pluggable devices,
  // however.
  response.flags = AUDIO_PDNF_HARDWIRED;
  response.plug_state_time = 0;

  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandGetClockDomain(
    const audio_stream_cmd_get_clock_domain_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_clock_domain_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;

  response.clock_domain = clock_domain_;

  zx_status_t status = stream_transceiver_.SendMessage(response_message);
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
  response.fifo_depth = fifo_depth_;

  zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandGetBuffer(audio_rb_cmd_get_buffer_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_rb_cmd_get_buffer_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;

  // This should be true since it's set as part of creating the channel that's carrying these
  // messages.
  FX_CHECK(selected_format_);

  if (!ring_buffer_) {
    // If we haven't created a ring buffer, we'll just drop this request.
    return;
  }
  FX_CHECK(ring_buffer_);

  // Dup our ring buffer VMO to send over the channel.
  zx::vmo dup;
  FX_CHECK(ring_buffer_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup) == ZX_OK);

  // Compute the buffer size in frames.
  auto frame_size =
      ::audio::utils::ComputeFrameSize(selected_format_->channels, selected_format_->sample_format);
  auto ring_buffer_frames = ring_buffer_size_ / frame_size;

  response.result = ZX_OK;
  response.num_ring_buffer_frames = ring_buffer_frames;
  response_message.handles_.push_back(dup.release());

  zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandStart(audio_rb_cmd_start_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_rb_cmd_start_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  if (!is_running_) {
    response.result = ZX_OK;
    response.start_time = async::Now(dispatcher_).get();
    is_running_ = true;
  } else {
    response.result = ZX_ERR_BAD_STATE;
  }
  zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriver::HandleCommandStop(audio_rb_cmd_stop_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_rb_cmd_stop_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  if (is_running_) {
    response.result = ZX_OK;
    is_running_ = false;
  } else {
    response.result = ZX_ERR_BAD_STATE;
  }
  zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

}  // namespace media::audio::testing
