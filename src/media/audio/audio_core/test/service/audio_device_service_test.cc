// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/test/service/audio_device_service_test.h"

namespace media::audio::test {

const std::string kManufacturer = "Test Manufacturer";
const std::string kProduct = "Test Product";
const std::vector<uint8_t> kUniqueId{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
const std::string kUniqueIdString = "000102030405060708090a0b0c0d0e0f";

void AudioDeviceServiceTest::SetUp() {
  HermeticAudioTest::SetUp();

  environment()->ConnectToService(audio_device_enumerator_.NewRequest());
  audio_device_enumerator_.set_error_handler(ErrorHandler());

  zx::channel local_channel;
  zx::channel remote_channel;
  zx_status_t status = zx::channel::create(0u, &local_channel, &remote_channel);
  EXPECT_EQ(ZX_OK, status);

  audio_device_enumerator_->AddDeviceByChannel(std::move(remote_channel), "test device", false);

  status = stream_transceiver_.Init(
      std::move(local_channel),
      fit::bind_member(this, &AudioDeviceServiceTest::OnInboundStreamMessage), ErrorHandler());
  EXPECT_EQ(ZX_OK, status);
}

void AudioDeviceServiceTest::TearDown() {
  ASSERT_TRUE(audio_device_enumerator_.is_bound());
  audio_device_enumerator_.events().OnDeviceRemoved = [this](uint64_t dev_token) {
    EXPECT_EQ(dev_token, device_token());
    devices_.clear();
  };

  ring_buffer_transceiver_.Close();
  stream_transceiver_.Close();
  ExpectCondition([this]() { return devices().empty(); });

  ASSERT_TRUE(audio_device_enumerator_.is_bound());
  audio_device_enumerator_.Unbind();

  HermeticAudioTest::TearDown();
}

void AudioDeviceServiceTest::OnInboundStreamMessage(MessageTransceiver::Message message) {
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

void AudioDeviceServiceTest::HandleCommandGetUniqueId(
    const audio_stream_cmd_get_unique_id_req_t& request) {
  MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_unique_id_resp>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  EXPECT_EQ(sizeof(response.unique_id.data), kUniqueId.size());
  memcpy(response.unique_id.data, kUniqueId.data(), kUniqueId.size());
  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void AudioDeviceServiceTest::HandleCommandGetString(
    const audio_stream_cmd_get_string_req_t& request) {
  std::string response_string;

  switch (request.id) {
    case AUDIO_STREAM_STR_ID_MANUFACTURER:
      response_string = kManufacturer;
      break;
    case AUDIO_STREAM_STR_ID_PRODUCT:
      response_string = kProduct;
      break;
    default:
      EXPECT_TRUE(false) << "Unrecognized string id " << request.id;
      return;
  }

  MessageTransceiver::Message response_message;
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

void AudioDeviceServiceTest::HandleCommandGetGain(const audio_stream_cmd_get_gain_req_t& request) {
  MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_gain_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.cur_mute = false;
  response.cur_agc = false;
  response.cur_gain = 1.0f;
  response.can_mute = true;
  response.can_agc = true;
  response.min_gain = -100.0f;
  response.max_gain = 3.0f;
  response.gain_step = 0.001f;
  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void AudioDeviceServiceTest::HandleCommandGetFormats(
    const audio_stream_cmd_get_formats_req_t& request) {
  MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_stream_cmd_get_formats_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.format_range_count = 1;
  response.first_format_range_ndx = 0;
  response.format_ranges[0].sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
  response.format_ranges[0].min_frames_per_second = 48000;
  response.format_ranges[0].max_frames_per_second = 48000;
  response.format_ranges[0].min_channels = 2;
  response.format_ranges[0].max_channels = 2;
  response.format_ranges[0].flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
  zx_status_t status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void AudioDeviceServiceTest::HandleCommandSetFormat(
    const audio_stream_cmd_set_format_req_t& request) {
  MessageTransceiver::Message response_message;
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
      fit::bind_member(this, &AudioDeviceServiceTest::OnInboundRingBufferMessage), ErrorHandler());
  EXPECT_EQ(ZX_OK, status);

  response_message.handles_.clear();
  response_message.handles_.push_back(std::move(remote_channel.get()));

  status = stream_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);
}

void AudioDeviceServiceTest::OnInboundRingBufferMessage(MessageTransceiver::Message message) {
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

void AudioDeviceServiceTest::HandleCommandGetFifoDepth(audio_rb_cmd_get_fifo_depth_req_t& request) {
  MessageTransceiver::Message response_message;
  auto& response = response_message.ResizeBytesAs<audio_rb_cmd_get_fifo_depth_resp_t>();
  response.hdr.transaction_id = request.hdr.transaction_id;
  response.hdr.cmd = request.hdr.cmd;
  response.result = ZX_OK;
  response.fifo_depth = 0;

  zx_status_t status = ring_buffer_transceiver_.SendMessage(response_message);
  EXPECT_EQ(ZX_OK, status);

  stream_config_complete_ = true;
}

void AudioDeviceServiceTest::HandleCommandGetBuffer(audio_rb_cmd_get_buffer_req_t& request) {}

void AudioDeviceServiceTest::HandleCommandStart(audio_rb_cmd_start_req_t& request) {
  ASSERT_TRUE(false) << "Unexpected START command received";
}

void AudioDeviceServiceTest::HandleCommandStop(audio_rb_cmd_stop_req_t& request) {
  ASSERT_TRUE(false) << "Unexpected STOP command received";
}

void AudioDeviceServiceTest::GetDevices() {
  devices_.clear();
  audio_device_enumerator_->GetDevices(
      [this](std::vector<fuchsia::media::AudioDeviceInfo> devices) mutable {
        for (auto device : devices) {
          devices_.push_back(device);
        }
      });
}

TEST_F(AudioDeviceServiceTest, AddDevice) {
  // Wait for interrogation and config through setting the format.
  set_stream_config_complete(false);
  ExpectCondition([this]() { return stream_config_complete(); });

  // Expect that the added device is enumerated via the device enumerator.
  GetDevices();
  ExpectCondition([this]() { return !devices().empty(); });

  EXPECT_EQ(1u, devices().size());
  auto device = devices()[0];
  EXPECT_EQ(kManufacturer + " " + kProduct, device.name);
  EXPECT_EQ(kUniqueIdString, device.unique_id);
  EXPECT_EQ(false, device.is_input);

  set_device_token(device.token_id);
}

}  // namespace media::audio::test
