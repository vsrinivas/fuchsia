// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <audio-proto-utils/format-utils.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/testing/fake_audio_driver.h"

namespace media::audio::testing {

FakeAudioDriverV1::FakeAudioDriverV1(zx::channel channel, async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher),
      stream_transceiver_(dispatcher),
      ring_buffer_transceiver_(dispatcher) {
  zx_status_t status = stream_transceiver_.Init(
      std::move(channel), fit::bind_member(this, &FakeAudioDriverV1::OnInboundStreamMessage),
      fit::bind_member(this, &FakeAudioDriverV1::OnInboundStreamError));
  FX_CHECK(status == ZX_OK);
  // Initially leave the driver 'stopped' so that it won't reply to any messages until |Start| is
  // called.
  stream_transceiver_.StopProcessing();
}

void FakeAudioDriverV1::Start() {
  stream_transceiver_.ResumeProcessing();
  if (ring_buffer_transceiver_.channel()) {
    ring_buffer_transceiver_.ResumeProcessing();
  }
  is_stopped_ = false;
}

void FakeAudioDriverV1::Stop() {
  stream_transceiver_.StopProcessing();
  if (ring_buffer_transceiver_.channel()) {
    ring_buffer_transceiver_.StopProcessing();
  }
  is_stopped_ = true;
}

fit::result<audio_cmd_t, zx_status_t> FakeAudioDriverV1::Step() {
  zx_status_t status = stream_transceiver_.ReadMessage();
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(last_stream_command_);
}

fit::result<audio_cmd_t, zx_status_t> FakeAudioDriverV1::StepRingBuffer() {
  zx_status_t status = ring_buffer_transceiver_.ReadMessage();
  if (status != ZX_OK) {
    return fit::error(status);
  }
  return fit::ok(last_ring_buffer_command_);
}

fzl::VmoMapper FakeAudioDriverV1::CreateRingBuffer(size_t size) {
  FX_CHECK(!ring_buffer_) << "Calling CreateRingBuffer multiple times is not supported";

  ring_buffer_size_ = size;
  fzl::VmoMapper mapper;
  mapper.CreateAndMap(ring_buffer_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr,
                      &ring_buffer_);
  return mapper;
}

void FakeAudioDriverV1::OnInboundStreamError(zx_status_t status) {}

void FakeAudioDriverV1::OnInboundStreamMessage(test::MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  last_stream_command_ = header.cmd;
  switch (header.cmd & ~AUDIO_FLAG_NO_ACK) {
    case AUDIO_STREAM_CMD_GET_FORMATS:
      HandleCommandGetFormats(message.BytesAs<audio_stream_cmd_get_formats_req_t>());
      break;
    case AUDIO_STREAM_CMD_SET_FORMAT:
      HandleCommandSetFormat(message.BytesAs<audio_stream_cmd_set_format_req_t>());
      break;
    case AUDIO_STREAM_CMD_GET_GAIN:
      HandleCommandGetGain(message.BytesAs<audio_stream_cmd_get_gain_req_t>());
      break;
    case AUDIO_STREAM_CMD_SET_GAIN:
      HandleCommandSetGain(message.BytesAs<audio_stream_cmd_set_gain_req_t>());
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

void FakeAudioDriverV1::HandleCommandGetUniqueId(
    const audio_stream_cmd_get_unique_id_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_unique_id_resp>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  std::memcpy(response.unique_id.data, uid_.data, sizeof(uid_.data));
  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriverV1::HandleCommandGetString(const audio_stream_cmd_get_string_req_t& request) {
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

void FakeAudioDriverV1::HandleCommandGetGain(const audio_stream_cmd_get_gain_req_t& request) {
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

void FakeAudioDriverV1::HandleCommandSetGain(const audio_stream_cmd_set_gain_req_t& request) {}

void FakeAudioDriverV1::HandleCommandGetFormats(const audio_stream_cmd_get_formats_req_t& request) {
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

void FakeAudioDriverV1::HandleCommandSetFormat(const audio_stream_cmd_set_format_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_set_format_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.result = ZX_OK;
  response.external_delay_nsec = external_delay_.get();

  // Note: Upon success, a channel used to control the audio buffer will also be returned.
  zx::channel local_channel;
  zx::channel remote_channel;
  zx_status_t status = zx::channel::create(0u, &local_channel, &remote_channel);
  EXPECT_EQ(ZX_OK, status);

  status = ring_buffer_transceiver_.Init(
      std::move(local_channel),
      fit::bind_member(this, &FakeAudioDriverV1::OnInboundRingBufferMessage),
      fit::bind_member(this, &FakeAudioDriverV1::OnInboundRingBufferError));
  EXPECT_EQ(ZX_OK, status);
  if (is_stopped_) {
    ring_buffer_transceiver_.StopProcessing();
  }

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

void FakeAudioDriverV1::HandleCommandPlugDetect(const audio_stream_cmd_plug_detect_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_plug_detect_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  if (hardwired_) {
    response.flags = AUDIO_PDNF_HARDWIRED;
  } else {
    response.flags = AUDIO_PDNF_CAN_NOTIFY;
    if (plugged_) {
      response.flags |= AUDIO_PDNF_PLUGGED;
    }
  }
  response.plug_state_time = 0;

  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriverV1::HandleCommandGetClockDomain(
    const audio_stream_cmd_get_clock_domain_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_clock_domain_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;

  response.clock_domain = clock_domain_;

  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriverV1::OnInboundRingBufferMessage(test::MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  last_ring_buffer_command_ = header.cmd;
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

void FakeAudioDriverV1::OnInboundRingBufferError(zx_status_t status) {}

void FakeAudioDriverV1::HandleCommandGetFifoDepth(audio_rb_cmd_get_fifo_depth_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_rb_cmd_get_fifo_depth_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.result = ZX_OK;
  response.fifo_depth = fifo_depth_;

  zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriverV1::HandleCommandGetBuffer(audio_rb_cmd_get_buffer_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_rb_cmd_get_buffer_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;

  notifications_per_ring_ = request.notifications_per_ring;

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

void FakeAudioDriverV1::HandleCommandStart(audio_rb_cmd_start_req_t& request) {
  test::MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_rb_cmd_start_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  if (!is_running_) {
    mono_start_time_ = async::Now(dispatcher_);
    is_running_ = true;

    response.result = ZX_OK;
    response.start_time = mono_start_time_.get();
  } else {
    response.result = ZX_ERR_BAD_STATE;
  }
  zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void FakeAudioDriverV1::HandleCommandStop(audio_rb_cmd_stop_req_t& request) {
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

void FakeAudioDriverV1::SendPositionNotification(zx::time timestamp, uint32_t position) {
  position_notify_timestamp_mono_ = timestamp;
  position_notify_position_bytes_ = position;

  if (is_running_ && (notifications_per_ring_ > 0)) {
    test::MessageTransceiver::Message response_message;
    auto& response = response_message.ResizeBytesAs<audio_rb_position_notify_t>();
    response.hdr.transaction_id = AUDIO_INVALID_TRANSACTION_ID;
    response.hdr.cmd = AUDIO_RB_POSITION_NOTIFY;

    response.monotonic_time = position_notify_timestamp_mono_.get();
    response.ring_buffer_pos = position_notify_position_bytes_;

    zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
    EXPECT_EQ(ZX_OK, status);
  }
}

}  // namespace media::audio::testing
