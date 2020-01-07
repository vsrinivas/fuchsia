// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/audio_driver_test.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <algorithm>
#include <cstring>

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::test {

static const struct {
  const char* path;
  DeviceType device_type;
} AUDIO_DEVNODES[] = {
    {.path = "/dev/class/audio-input", .device_type = DeviceType::Input},
    {.path = "/dev/class/audio-output", .device_type = DeviceType::Output},
};

// static
bool AudioDriverTest::no_input_devices_found_ = false;
bool AudioDriverTest::no_output_devices_found_ = false;

// static
void AudioDriverTest::SetUpTestSuite() {
  // For verbose logging, set to -media::audio::TRACE or -media::audio::SPEW
#ifdef NDEBUG
  Logging::Init(FX_LOG_WARNING, {"audio_driver_test"});
#else
  Logging::Init(FX_LOG_INFO, {"audio_driver_test"});
#endif
}

// static
std::atomic_uint32_t AudioDriverTest::unique_transaction_id_ = 0;

// static
zx_txid_t AudioDriverTest::NextTransactionId() {
  uint32_t trans_id = ++unique_transaction_id_;
  if (trans_id == AUDIO_INVALID_TRANSACTION_ID) {
    trans_id = ++unique_transaction_id_;
  }
  return trans_id;
}

void AudioDriverTest::SetUp() {
  TestFixture::SetUp();

  stream_channel_.reset();
}

void AudioDriverTest::TearDown() {
  ring_buffer_transceiver_.Close();
  stream_transceiver_.Close();

  watchers_.clear();

  TestFixture::TearDown();
}

bool AudioDriverTest::WaitForDevice(DeviceType device_type) {
  if (device_type == DeviceType::Input && AudioDriverTest::no_input_devices_found_) {
    return false;
  }
  if (device_type == DeviceType::Output && AudioDriverTest::no_output_devices_found_) {
    return false;
  }

  device_type_ = device_type;
  bool enumeration_done = false;

  // Set up the watchers, etc. If any fail, automatically stop monitoring all device sources.
  for (const auto& devnode : AUDIO_DEVNODES) {
    if (device_type != devnode.device_type) {
      continue;
    }

    auto watcher = fsl::DeviceWatcher::CreateWithIdleCallback(
        devnode.path,
        [this, device_type](int dir_fd, const std::string& filename) {
          AUD_VLOG(TRACE) << "'" << filename << "' dir_fd " << dir_fd;
          this->AddDevice(dir_fd, filename, device_type);
        },
        [&enumeration_done]() { enumeration_done = true; });

    if (watcher == nullptr) {
      EXPECT_FALSE(watcher == nullptr)
          << "AudioDriverTest failed to create DeviceWatcher for '" << devnode.path << "'.";
      watchers_.clear();
      return false;
    }
    watchers_.emplace_back(std::move(watcher));
  }
  //
  // ... or ...
  //
  // Receive a call to AddDeviceByChannel(std::move(stream_channel), name, device_type);
  //

  RunLoopUntil([&enumeration_done]() { return enumeration_done; });

  // If we timed out waiting for devices, this target may not have any. Don't waste further time.
  if (!stream_channel_ready_) {
    if (device_type == DeviceType::Input) {
      AudioDriverTest::no_input_devices_found_ = true;
    } else {
      AudioDriverTest::no_output_devices_found_ = true;
    }
    FX_LOGS(WARNING) << "*** No audio " << ((device_type == DeviceType::Input) ? "input" : "output")
                     << " devices detected on this target. ***";
    return false;
  }

  // ASSERT that we can communicate with the driver at all.
  EXPECT_TRUE(stream_channel_.is_valid());
  EXPECT_TRUE(stream_channel_ready_);

  zx_status_t status = stream_transceiver_.Init(
      std::move(stream_channel_), fit::bind_member(this, &AudioDriverTest::OnInboundStreamMessage),
      ErrorHandler());
  EXPECT_EQ(ZX_OK, status);

  return stream_channel_ready_ && (status == ZX_OK);
}

void AudioDriverTest::AddDevice(int dir_fd, const std::string& name, DeviceType device_type) {
  // TODO(mpuryear): on systems with more than one audio device of a given type, test them all.
  if (stream_channel_ready_) {
    FX_LOGS(WARNING) << "More than one device detected. For now, we need to ignore it.";
    return;
  }

  // Open the device node.
  fbl::unique_fd dev_node(openat(dir_fd, name.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FX_LOGS(ERROR) << "AudioDriverTest failed to open device node at \"" << name << "\". ("
                   << strerror(errno) << " : " << errno << ")";
    FAIL();
  }

  // Obtain the FDIO device channel, wrap it in a sync proxy, use that to get the stream channel.
  zx::channel dev_channel;
  zx_status_t status =
      fdio_get_service_handle(dev_node.release(), dev_channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to obtain FDIO service channel to audio "
                            << ((device_type == DeviceType::Input) ? "input" : "output");
    FAIL();
  }

  // Obtain the stream channel
  auto dev =
      fidl::InterfaceHandle<fuchsia::hardware::audio::Device>(std::move(dev_channel)).BindSync();
  fidl::InterfaceRequest<fuchsia::hardware::audio::StreamConfig> intf_req = {};

  status = dev->GetChannel(&intf_req);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to open channel to audio "
                            << ((device_type == DeviceType::Input) ? "input" : "output");
    FAIL();
  }
  stream_channel_ = intf_req.TakeChannel();

  AUD_VLOG(TRACE) << "Successfully opened devnode '" << name << "' for audio "
                  << ((device_type == DeviceType::Input) ? "input" : "output");
  stream_channel_ready_ = true;
}

// Stream channel requests
//
// Request the driver's unique ID.
// TODO(mpuryear): ensure that this differs between input and output.
void AudioDriverTest::RequestUniqueId() {
  if (error_occurred_) {
    return;
  }

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_unique_id_req_t>();
  unique_id_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = unique_id_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_UNIQUE_ID;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  RunLoopUntil([this]() { return received_get_unique_id_; });
}

// Request that the driver return its manufacturer string.
void AudioDriverTest::RequestManufacturerString() {
  if (error_occurred_) {
    return;
  }

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_string_req_t>();
  manufacturer_string_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = manufacturer_string_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_STRING;

  request.id = AUDIO_STREAM_STR_ID_MANUFACTURER;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_get_string_manufacturer_ || error_occurred_; });
}

// Request that the driver return its product string.
void AudioDriverTest::RequestProductString() {
  if (error_occurred_) {
    return;
  }

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_string_req_t>();
  product_string_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = product_string_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_STRING;

  request.id = AUDIO_STREAM_STR_ID_PRODUCT;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_get_string_product_ || error_occurred_; });
}

// Request that the driver return its gain capabilities and current state.
void AudioDriverTest::RequestGain() {
  if (error_occurred_) {
    return;
  }

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_gain_req_t>();
  get_gain_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_gain_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_GAIN;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  RunLoopUntil([this]() { return received_get_gain_; });
}

// Determine an appropriate gain state to request, then call other method to request to the
// driver. This method assumes that the driver has already successfully responded to a GetGain
// request.
void AudioDriverTest::RequestSetGain() {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(received_get_gain_);

  if (max_gain_ == min_gain_) {
    FX_LOGS(WARNING) << "*** Audio " << ((device_type_ == DeviceType::Input) ? "input" : "output")
                     << " has fixed gain (" << cur_gain_ << " dB). Skipping SetGain test. ***";
    return;
  }

  set_gain_ = min_gain_;
  if (cur_gain_ == min_gain_) {
    set_gain_ += gain_step_;
  }

  audio_set_gain_flags_t flags = AUDIO_SGF_GAIN_VALID;
  if (can_mute_) {
    flags |= AUDIO_SGF_MUTE_VALID;
    if (!cur_mute_) {
      flags |= AUDIO_SGF_MUTE;
    }
  }
  if (can_agc_) {
    flags |= AUDIO_SGF_AGC_VALID;
    if (!cur_agc_) {
      flags |= AUDIO_SGF_AGC;
    }
  }
  RequestSetGain(flags, set_gain_);
}

// Request that the driver set its gain state to the specified gain_db and flags.
// This method assumes that the driver has already successfully responded to a GetGain request.
void AudioDriverTest::RequestSetGain(audio_set_gain_flags_t flags, float gain_db) {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(received_get_gain_);

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_set_gain_req_t>();
  set_gain_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = set_gain_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_SET_GAIN;

  request.flags = flags;
  request.gain = gain_db;
  set_mute_ = (flags & AUDIO_SGF_MUTE_VALID) ? (flags & AUDIO_SGF_MUTE) : cur_mute_;
  set_agc_ = (flags & AUDIO_SGF_AGC_VALID) ? (flags & AUDIO_SGF_AGC) : cur_agc_;
  set_gain_ = (flags & AUDIO_SGF_GAIN_VALID) ? gain_db : cur_gain_;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_set_gain_ || error_occurred_; });
}

// Request that the driver return the format ranges that it supports.
void AudioDriverTest::RequestFormats() {
  if (error_occurred_) {
    return;
  }

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_formats_req_t>();
  get_formats_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_formats_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_FORMATS;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  RunLoopUntil([this]() { return received_get_formats_; });
}

// For the channelization and sample_format that we've set, determine the size of each frame.
// This method assumes that the driver has already successfully responded to a SetFormat request.
void AudioDriverTest::CalculateFrameSize() {
  if (error_occurred_ || !received_set_format_) {
    return;
  }

  switch (sample_format_) {
    case AUDIO_SAMPLE_FORMAT_8BIT:
      frame_size_ = num_channels_;
      break;

    case AUDIO_SAMPLE_FORMAT_16BIT:
      frame_size_ = num_channels_ * sizeof(int16_t);
      break;

    case AUDIO_SAMPLE_FORMAT_20BIT_PACKED:
      frame_size_ = (num_channels_ * 5 + 3) / 4;
      break;

    case AUDIO_SAMPLE_FORMAT_24BIT_PACKED:
      frame_size_ = num_channels_ * 3;
      break;

    case AUDIO_SAMPLE_FORMAT_20BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_24BIT_IN32:
    case AUDIO_SAMPLE_FORMAT_32BIT:
      frame_size_ = num_channels_ * sizeof(int32_t);
      break;

    case AUDIO_SAMPLE_FORMAT_32BIT_FLOAT:
      frame_size_ = num_channels_ * sizeof(float);
      break;

    case AUDIO_SAMPLE_FORMAT_BITSTREAM:
    default:
      ASSERT_TRUE(false) << "Unknown sample_format_ " << std::hex << sample_format_;
      frame_size_ = 0;
      break;
  }
}

void AudioDriverTest::SelectFirstFormat() {
  if (received_get_formats_) {
    // strip off the UNSIGNED and INVERT_ENDIAN bits...
    auto first_range = format_ranges_.front();
    auto first_format = first_range.sample_formats & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK;
    ASSERT_NE(first_format, 0u);

    // just keep the lowest sample format bit.
    audio_sample_format_t bit = 1;
    while ((first_format & bit) == 0) {
      bit <<= 1;
    }
    first_format &= bit;

    frame_rate_ = first_range.min_frames_per_second;
    sample_format_ = first_format;
    num_channels_ = first_range.min_channels;
  }
}

void AudioDriverTest::SelectLastFormat() {
  if (received_get_formats_) {
    // strip off the UNSIGNED and INVERT_ENDIAN bits...
    auto last_range = format_ranges_.back();
    auto last_format = last_range.sample_formats & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK;
    ASSERT_NE(last_format, 0u);

    // and just keep the highest remaining sample format bit.
    while (last_format & (last_format - 1)) {
      last_format &= (last_format - 1);
    }

    frame_rate_ = last_range.max_frames_per_second;
    sample_format_ = last_format;
    num_channels_ = last_range.max_channels;
  }
}

// Request that driver set format to the lowest rate/channelization of the first range reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AudioDriverTest::RequestSetFormatMin() {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(received_get_formats_);
  ASSERT_GT(format_ranges_.size(), 0u);

  SelectFirstFormat();

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_set_format_req_t>();
  set_format_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = set_format_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;

  request.frames_per_second = frame_rate_;
  request.sample_format = sample_format_;
  request.channels = num_channels_;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil(
      [this]() { return (received_set_format_ && ring_buffer_channel_ready_) || error_occurred_; });
  CalculateFrameSize();
}

// Request that driver set format to the highest rate/channelization of the final range reported.
// This method assumes that the driver has already successfully responded to a GetFormats request.
void AudioDriverTest::RequestSetFormatMax() {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(received_get_formats_);
  ASSERT_GT(format_ranges_.size(), 0u);

  SelectLastFormat();

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_set_format_req_t>();
  set_format_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = set_format_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_SET_FORMAT;

  request.frames_per_second = frame_rate_;
  request.sample_format = sample_format_;
  request.channels = num_channels_;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil(
      [this]() { return (received_set_format_ && ring_buffer_channel_ready_) || error_occurred_; });
  CalculateFrameSize();
}

// Request that driver retrieve the current plug detection state and capabilities.
void AudioDriverTest::RequestPlugDetect() {
  if (error_occurred_) {
    return;
  }

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_plug_detect_req_t>();
  plug_detect_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = plug_detect_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_PLUG_DETECT;

  request.flags = AUDIO_PDF_ENABLE_NOTIFICATIONS;
  should_plug_notify_ = true;

  EXPECT_EQ(ZX_OK, stream_transceiver_.SendMessage(request_message));

  RunLoopUntil([this]() { return received_plug_detect_; });
}

// Ring-buffer channel requests
//
// Request that the driver return the FIFO depth (in bytes), at the currently set format.
// This method relies on the ring buffer channel, received with response to a successful
// SetFormat.
void AudioDriverTest::RequestFifoDepth() {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(ring_buffer_channel_ready_);

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_rb_cmd_get_fifo_depth_req_t>();
  get_fifo_depth_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_fifo_depth_transaction_id_;
  request.hdr.cmd = AUDIO_RB_CMD_GET_FIFO_DEPTH;

  EXPECT_EQ(ZX_OK, ring_buffer_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_get_fifo_depth_ || error_occurred_; });
}

// Request that the driver return a VMO handle for the ring buffer, at the currently set format.
// This method relies on the ring buffer channel, received with response to a successful
// SetFormat.
void AudioDriverTest::RequestBuffer(uint32_t min_ring_buffer_frames,
                                    uint32_t notifications_per_ring) {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(ring_buffer_channel_ready_);

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_rb_cmd_get_buffer_req_t>();
  get_buffer_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_buffer_transaction_id_;
  request.hdr.cmd = AUDIO_RB_CMD_GET_BUFFER;

  request.min_ring_buffer_frames = min_ring_buffer_frames;
  min_ring_buffer_frames_ = min_ring_buffer_frames;

  request.notifications_per_ring = notifications_per_ring;
  notifications_per_ring_ = notifications_per_ring;

  EXPECT_EQ(ZX_OK, ring_buffer_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_get_buffer_ || error_occurred_; });
}

// Request that the driver start the ring buffer engine, responding with the start_time.
// This method assumes that the ring buffer VMO was received in a successful GetBuffer response.
void AudioDriverTest::RequestStart() {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(ring_buffer_ready_);

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_rb_cmd_start_req_t>();
  start_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = start_transaction_id_;
  request.hdr.cmd = AUDIO_RB_CMD_START;

  auto send_time = zx::clock::get_monotonic().get();
  EXPECT_EQ(ZX_OK, ring_buffer_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_start_ || error_occurred_; });

  EXPECT_GT(start_time_, send_time);
  // TODO(mpuryear): validate start_time is not too far in the future (it includes FIFO delay).
}

// Request that the driver stop the ring buffer engine, including quieting position notifications.
// This method assumes that the ring buffer engine has previously been successfully started.
void AudioDriverTest::RequestStop() {
  if (error_occurred_) {
    return;
  }

  ASSERT_TRUE(received_start_);

  MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_rb_cmd_stop_req_t>();
  stop_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = stop_transaction_id_;
  request.hdr.cmd = AUDIO_RB_CMD_STOP;

  EXPECT_EQ(ZX_OK, ring_buffer_transceiver_.SendMessage(request_message));

  // This command can return an error, so we check for error_occurred_ as well
  RunLoopUntil([this]() { return received_stop_ || error_occurred_; });
}

// Handle an incoming stream channel message (generally a response from a previous request)
void AudioDriverTest::OnInboundStreamMessage(MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  switch (header.cmd) {
    case AUDIO_STREAM_CMD_GET_UNIQUE_ID:
      HandleGetUniqueIdResponse(message.BytesAs<audio_stream_cmd_get_unique_id_resp_t>());
      break;

    case AUDIO_STREAM_CMD_GET_STRING:
      HandleGetStringResponse(message.BytesAs<audio_stream_cmd_get_string_resp_t>());
      break;

    case AUDIO_STREAM_CMD_GET_GAIN:
      HandleGetGainResponse(message.BytesAs<audio_stream_cmd_get_gain_resp_t>());
      break;

    case AUDIO_STREAM_CMD_SET_GAIN:
      HandleSetGainResponse(message.BytesAs<audio_stream_cmd_set_gain_resp_t>());
      break;

    case AUDIO_STREAM_CMD_GET_FORMATS:
      HandleGetFormatsResponse(message.BytesAs<audio_stream_cmd_get_formats_resp_t>());
      break;

    case AUDIO_STREAM_CMD_SET_FORMAT:
      HandleSetFormatResponse(message.BytesAs<audio_stream_cmd_set_format_resp_t>());
      // On success, a channel used to control the audio buffer will be returned.
      ExtractRingBufferChannel(message);
      break;

    case AUDIO_STREAM_CMD_PLUG_DETECT:
      HandlePlugDetectResponse(message.BytesAs<audio_stream_cmd_plug_detect_resp_t>());
      break;

    case AUDIO_STREAM_PLUG_DETECT_NOTIFY:
      HandlePlugDetectNotify(message.BytesAs<audio_stream_cmd_plug_detect_resp_t>());
      break;

    default:
      EXPECT_TRUE(false) << "Unrecognized header.cmd value " << header.cmd;
      break;
  }
}

// Validate just the command portion of the response header.
bool AudioDriverTest::ValidateResponseCommand(audio_cmd_hdr header, audio_cmd_t expected_command) {
  EXPECT_EQ(header.cmd, expected_command) << "Unexpected command!";

  return (expected_command == header.cmd);
}

// Validate just the transaction ID portion of the response header.
void AudioDriverTest::ValidateResponseTransaction(audio_cmd_hdr header,
                                                  zx_txid_t expected_transaction_id) {
  EXPECT_EQ(header.transaction_id, expected_transaction_id) << "Unexpected transaction ID!";
}

// Validate the entire response header.
bool AudioDriverTest::ValidateResponseHeader(audio_cmd_hdr header,
                                             zx_txid_t expected_transaction_id,
                                             audio_cmd_t expected_command) {
  ValidateResponseTransaction(header, expected_transaction_id);
  return ValidateResponseCommand(header, expected_command);
}

// Handle a get_unique_id response on the stream channel.
void AudioDriverTest::HandleGetUniqueIdResponse(
    const audio_stream_cmd_get_unique_id_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, unique_id_transaction_id_,
                              AUDIO_STREAM_CMD_GET_UNIQUE_ID)) {
    return;
  }

  EXPECT_EQ(sizeof(response.unique_id.data), sizeof(audio_stream_unique_id_t));
  memcpy(unique_id_.data(), response.unique_id.data, kUniqueIdLength);

  char id_buf[2 * kUniqueIdLength + 1];
  std::snprintf(id_buf, sizeof(id_buf),
                "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", unique_id_[0],
                unique_id_[1], unique_id_[2], unique_id_[3], unique_id_[4], unique_id_[5],
                unique_id_[6], unique_id_[7], unique_id_[8], unique_id_[9], unique_id_[10],
                unique_id_[11], unique_id_[12], unique_id_[13], unique_id_[14], unique_id_[15]);
  AUD_VLOG(TRACE) << "Received unique_id " << id_buf;

  received_get_unique_id_ = true;
}

// Handle a get_string response on the stream channel (either mfr or prod).
void AudioDriverTest::HandleGetStringResponse(const audio_stream_cmd_get_string_resp_t& response) {
  if (!ValidateResponseCommand(response.hdr, AUDIO_STREAM_CMD_GET_STRING)) {
    return;
  }

  constexpr auto kMaxStringLength =
      sizeof(audio_stream_cmd_get_string_resp_t) - sizeof(audio_cmd_hdr_t) - (3 * sizeof(uint32_t));
  EXPECT_LE(response.strlen, kMaxStringLength);
  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  auto response_str = reinterpret_cast<const char*>(response.str);
  if (response.id == AUDIO_STREAM_STR_ID_MANUFACTURER) {
    ValidateResponseTransaction(response.hdr, manufacturer_string_transaction_id_);

    manufacturer_ = std::string(response_str, response.strlen);
    received_get_string_manufacturer_ = true;
  } else if (response.id == AUDIO_STREAM_STR_ID_PRODUCT) {
    ValidateResponseTransaction(response.hdr, product_string_transaction_id_);

    product_ = std::string(response_str, response.strlen);
    received_get_string_product_ = true;
  } else {
    ASSERT_TRUE(false) << "Unrecognized string ID received: " << response.id;
  }
}

// Handle a get_gain response on the stream channel.
void AudioDriverTest::HandleGetGainResponse(const audio_stream_cmd_get_gain_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_gain_transaction_id_, AUDIO_STREAM_CMD_GET_GAIN)) {
    return;
  }

  cur_mute_ = response.cur_mute;
  can_mute_ = response.can_mute;
  cur_agc_ = response.cur_agc;
  can_agc_ = response.can_agc;
  cur_gain_ = response.cur_gain;
  min_gain_ = response.min_gain;
  max_gain_ = response.max_gain;
  gain_step_ = response.gain_step;

  if (cur_mute_) {
    EXPECT_TRUE(can_mute_);
  }
  if (cur_agc_) {
    EXPECT_TRUE(can_agc_);
  }
  EXPECT_GE(cur_gain_, min_gain_);
  EXPECT_LE(cur_gain_, max_gain_);
  if (max_gain_ > min_gain_) {
    EXPECT_GT(gain_step_, 0.0f);
  } else {
    EXPECT_EQ(gain_step_, 0.0f);
  }

  received_get_gain_ = true;
}

// Handle a set_gain response on the stream channel.
void AudioDriverTest::HandleSetGainResponse(const audio_stream_cmd_set_gain_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, set_gain_transaction_id_, AUDIO_STREAM_CMD_SET_GAIN)) {
    return;
  }
  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  cur_mute_ = response.cur_mute;
  EXPECT_EQ(cur_mute_, set_mute_);
  if (cur_mute_) {
    EXPECT_TRUE(can_mute_);
  }

  cur_agc_ = response.cur_agc;
  EXPECT_EQ(cur_agc_, set_agc_);
  if (cur_agc_) {
    EXPECT_TRUE(can_agc_);
  }

  cur_gain_ = response.cur_gain;
  EXPECT_EQ(cur_gain_, set_gain_);
  EXPECT_GE(cur_gain_, min_gain_);
  EXPECT_LE(cur_gain_, max_gain_);

  received_set_gain_ = true;
}

// Handle a get_formats response on the stream channel. This response may be a multi-part.
void AudioDriverTest::HandleGetFormatsResponse(
    const audio_stream_cmd_get_formats_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_formats_transaction_id_,
                              AUDIO_STREAM_CMD_GET_FORMATS)) {
    return;
  }

  EXPECT_GT(response.format_range_count, 0u);
  EXPECT_LT(response.first_format_range_ndx, response.format_range_count);
  EXPECT_EQ(response.first_format_range_ndx, next_format_range_ndx_);

  if (response.first_format_range_ndx == 0) {
    get_formats_range_count_ = response.format_range_count;
    format_ranges_.clear();
  } else {
    EXPECT_EQ(response.format_range_count, get_formats_range_count_)
        << "Format range count cannot change over multiple get_formats responses";
  }
  auto num_ranges =
      std::min<uint16_t>(response.format_range_count - response.first_format_range_ndx,
                         AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE);

  for (auto i = 0; i < num_ranges; ++i) {
    EXPECT_NE(response.format_ranges[i].sample_formats & ~AUDIO_SAMPLE_FORMAT_FLAG_MASK, 0u);

    EXPECT_GE(response.format_ranges[i].min_frames_per_second,
              fuchsia::media::MIN_PCM_FRAMES_PER_SECOND);
    EXPECT_LE(response.format_ranges[i].max_frames_per_second,
              fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
    EXPECT_LE(response.format_ranges[i].min_frames_per_second,
              response.format_ranges[i].max_frames_per_second);

    EXPECT_GE(response.format_ranges[i].min_channels, fuchsia::media::MIN_PCM_CHANNEL_COUNT);
    EXPECT_LE(response.format_ranges[i].max_channels, fuchsia::media::MAX_PCM_CHANNEL_COUNT);
    EXPECT_LE(response.format_ranges[i].min_channels, response.format_ranges[i].max_channels);

    EXPECT_NE(response.format_ranges[i].flags, 0u);

    format_ranges_.push_back(response.format_ranges[i]);
  }

  next_format_range_ndx_ += num_ranges;
  if (next_format_range_ndx_ == response.format_range_count) {
    EXPECT_EQ(response.format_range_count, format_ranges_.size());
    received_get_formats_ = true;
  }
}

// Handle a set_format response on the stream channel. After, we will extract a ring buffer
// channel.
void AudioDriverTest::HandleSetFormatResponse(const audio_stream_cmd_set_format_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, set_format_transaction_id_,
                              AUDIO_STREAM_CMD_SET_FORMAT)) {
    return;
  }

  if (response.result != ZX_OK) {
    if (response.result == ZX_ERR_ACCESS_DENIED) {
      AUD_LOG(WARNING)
          << "ZX_ERR_ACCESS_DENIED: audio_core may already be connected to this device";
    }
    error_occurred_ = true;
    FAIL();
  }

  external_delay_nsec_ = response.external_delay_nsec;

  received_set_format_ = true;
}

// In concert with incoming SetFormat response on stream channel, extract the ring-buffer channel.
// With it, initialize the message transceiver that will handle messages to/from this channel.
void AudioDriverTest::ExtractRingBufferChannel(MessageTransceiver::Message message) {
  if (!received_set_format_) {
    return;
  }

  ASSERT_EQ(message.handles_.size(), 1u);

  EXPECT_EQ(
      ring_buffer_transceiver_.Init(
          zx::channel(message.handles_[0]),
          fit::bind_member(this, &AudioDriverTest::OnInboundRingBufferMessage), ErrorHandler()),
      ZX_OK);
  message.handles_.clear();

  ring_buffer_channel_ready_ = true;
}

// Handle plug_detection on the stream channel (shared across response and notification).
void AudioDriverTest::HandlePlugDetect(audio_pd_notify_flags_t flags, zx_time_t plug_state_time) {
  if (received_plug_detect_) {
    EXPECT_EQ(hardwired_, (flags & AUDIO_PDNF_HARDWIRED));
  }
  hardwired_ = flags & AUDIO_PDNF_HARDWIRED;

  if (received_plug_detect_) {
    EXPECT_EQ(can_plug_notify_, (flags & AUDIO_PDNF_CAN_NOTIFY));
  }
  can_plug_notify_ = flags & AUDIO_PDNF_CAN_NOTIFY;
  plugged_ = flags & AUDIO_PDNF_PLUGGED;

  plug_state_time_ = plug_state_time;
  EXPECT_LT(plug_state_time_, zx::clock::get_monotonic().get());

  AUD_VLOG(TRACE) << "Plug_state_time: " << plug_state_time;
}

// Handle a plug_detect response on the stream channel (response solicited by client).
void AudioDriverTest::HandlePlugDetectResponse(
    const audio_stream_cmd_plug_detect_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, plug_detect_transaction_id_,
                              AUDIO_STREAM_CMD_PLUG_DETECT)) {
    return;
  }

  HandlePlugDetect(response.flags, response.plug_state_time);
  received_plug_detect_ = true;
}

// Handle a plug_detect notification on the stream channel (async message not solicited by
// client).
void AudioDriverTest::HandlePlugDetectNotify(const audio_stream_cmd_plug_detect_resp_t& notify) {
  if (!ValidateResponseHeader(notify.hdr, AUDIO_INVALID_TRANSACTION_ID,
                              AUDIO_STREAM_PLUG_DETECT_NOTIFY)) {
    return;
  }

  EXPECT_FALSE(hardwired_);
  EXPECT_TRUE(can_plug_notify_);
  EXPECT_TRUE(should_plug_notify_);

  HandlePlugDetect(notify.flags, notify.plug_state_time);
  received_plug_detect_notify_ = true;

  AUD_LOG(ERROR) << "Driver autonomously generated an asynchronous plug detect notification";
}

// Handle all incoming response message types, on the ring buffer channel.
void AudioDriverTest::OnInboundRingBufferMessage(MessageTransceiver::Message message) {
  auto& header = message.BytesAs<audio_cmd_hdr_t>();
  switch (header.cmd) {
    case AUDIO_RB_CMD_GET_FIFO_DEPTH:
      HandleGetFifoDepthResponse(message.BytesAs<audio_rb_cmd_get_fifo_depth_resp_t>());
      break;

    case AUDIO_RB_CMD_GET_BUFFER:
      HandleGetBufferResponse(message.BytesAs<audio_rb_cmd_get_buffer_resp_t>());

      // On success, a VMO for the ring buffer will be returned.
      ExtractRingBuffer(message);
      break;

    case AUDIO_RB_CMD_START:
      HandleStartResponse(message.BytesAs<audio_rb_cmd_start_resp_t>());
      break;

    case AUDIO_RB_CMD_STOP:
      HandleStopResponse(message.BytesAs<audio_rb_cmd_stop_resp_t>());
      break;

    case AUDIO_RB_POSITION_NOTIFY:
      HandlePositionNotify(message.BytesAs<audio_rb_position_notify_t>());
      break;

    default:
      EXPECT_TRUE(false) << "Unrecognized header.cmd value " << header.cmd;
      break;
  }
}

// Handle a get_fifo_depth response on the ring buffer channel.
void AudioDriverTest::HandleGetFifoDepthResponse(
    const audio_rb_cmd_get_fifo_depth_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_fifo_depth_transaction_id_,
                              AUDIO_RB_CMD_GET_FIFO_DEPTH)) {
    return;
  }

  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  fifo_depth_ = response.fifo_depth;

  received_get_fifo_depth_ = true;
}

// Handle a get_buffer response on the ring buffer channel.
void AudioDriverTest::HandleGetBufferResponse(const audio_rb_cmd_get_buffer_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_buffer_transaction_id_, AUDIO_RB_CMD_GET_BUFFER)) {
    return;
  }

  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  EXPECT_GE(response.num_ring_buffer_frames, min_ring_buffer_frames_);
  ring_buffer_frames_ = response.num_ring_buffer_frames;

  received_get_buffer_ = true;
}

// Given the GET_BUFFER response message, retrieve the ring buffer VMO handle and save it.
void AudioDriverTest::ExtractRingBuffer(MessageTransceiver::Message get_buffer_response) {
  ASSERT_TRUE(received_get_buffer_);

  EXPECT_EQ(get_buffer_response.handles_.size(), 1u);
  zx::vmo ring_buffer_vmo = zx::vmo(get_buffer_response.handles_[0]);
  get_buffer_response.handles_.clear();
  EXPECT_TRUE(ring_buffer_vmo.is_valid());

  const zx_vm_option_t option_flags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
  EXPECT_EQ(
      ring_buffer_.CreateAndMap(ring_buffer_frames_ * frame_size_, option_flags, nullptr,
                                &ring_buffer_vmo, ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER),
      ZX_OK);

  AUD_VLOG(TRACE) << "Mapping size: " << ring_buffer_frames_ * frame_size_;

  ring_buffer_ready_ = true;
}

// Handle a start response on the ring buffer channel.
void AudioDriverTest::HandleStartResponse(const audio_rb_cmd_start_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, start_transaction_id_, AUDIO_RB_CMD_START)) {
    return;
  }

  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  ASSERT_GT(response.start_time, 0u);
  start_time_ = response.start_time;

  received_start_ = true;
}

// Handle a stop response on the ring buffer channel. Clear out any previous position notification
// count, to enable us to detect whether any were received after the STOP command was processed.
void AudioDriverTest::HandleStopResponse(const audio_rb_cmd_stop_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, stop_transaction_id_, AUDIO_RB_CMD_STOP)) {
    return;
  }

  if (response.result != ZX_OK) {
    error_occurred_ = true;
    FAIL();
  }

  position_notification_count_ = 0;
  received_stop_ = true;
}

// Handle a position notification on the ring buffer channel.
void AudioDriverTest::HandlePositionNotify(const audio_rb_position_notify_t& notify) {
  if (!ValidateResponseHeader(notify.hdr, get_position_transaction_id_, AUDIO_RB_POSITION_NOTIFY)) {
    return;
  }

  EXPECT_GT(notifications_per_ring_, 0u);

  auto now = zx::clock::get_monotonic().get();
  EXPECT_LT(start_time_, now);
  EXPECT_LT(notify.monotonic_time, now);

  if (position_notification_count_) {
    EXPECT_GT(notify.monotonic_time, start_time_);
    EXPECT_GT(notify.monotonic_time, last_monotonic_time_);
  } else {
    EXPECT_GE(notify.monotonic_time, start_time_);
  }

  last_monotonic_time_ = notify.monotonic_time;
  ring_buffer_position_ = notify.ring_buffer_pos;
  EXPECT_LT(ring_buffer_position_, ring_buffer_frames_ * frame_size_);

  ++position_notification_count_;

  AUD_VLOG(TRACE) << "Position: " << ring_buffer_position_
                  << ", notification_count: " << position_notification_count_;
}

// Wait for the specified number of position notifications, or timeout at 60 seconds.
void AudioDriverTest::ExpectPositionNotifyCount(uint32_t count) {
  if (error_occurred_) {
    return;
  }

  RunLoopUntil([this, count]() { return position_notification_count_ >= count; });

  auto timestamp_duration = last_monotonic_time_ - start_time_;
  auto observed_duration = zx::clock::get_monotonic().get() - start_time_;
  ASSERT_GE(position_notification_count_, count) << "No position notifications received";

  ASSERT_NE(frame_rate_ * notifications_per_ring_, 0u);
  auto ns_per_notification =
      (zx::sec(1) * ring_buffer_frames_) / (frame_rate_ * notifications_per_ring_);
  auto expected_min_time = ns_per_notification.get() * (count - 1);
  auto expected_time = ns_per_notification.get() * count;
  auto expected_max_time = ns_per_notification.get() * (count + 2);

  AUD_VLOG(TRACE) << "Timestamp delta from min/ideal/max: " << std::setw(10)
                  << (expected_min_time - timestamp_duration) << " : " << std::setw(10)
                  << (expected_time - timestamp_duration) << " : " << std::setw(10)
                  << (expected_max_time - timestamp_duration);
  EXPECT_GE(timestamp_duration, expected_min_time);
  EXPECT_LT(timestamp_duration, expected_max_time);

  AUD_VLOG(TRACE) << "Observed delta from min/ideal/max : " << std::setw(10)
                  << (expected_min_time - observed_duration) << " : " << std::setw(10)
                  << (expected_time - observed_duration) << " : " << std::setw(10)
                  << (expected_max_time - observed_duration);
  EXPECT_GT(observed_duration, expected_min_time);
}

// After waiting for one second, we should NOT have received any position notifications.
void AudioDriverTest::ExpectNoPositionNotifications() {
  if (error_occurred_) {
    return;
  }

  zx::nanosleep(zx::deadline_after(zx::sec(1)));
  RunLoopUntilIdle();

  EXPECT_EQ(position_notification_count_, 0u);
}

//
// Test cases that target each of the various driver commands
//

// Stream channel commands
//
// AUDIO_STREAM_CMD_GET_UNIQUE_ID
// For input stream, verify a valid GET_UNIQUE_ID response is successfully received.
TEST_F(AudioDriverTest, InputGetUniqueId) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestUniqueId();
}

// For output stream, verify a valid GET_UNIQUE_ID response is successfully received.
TEST_F(AudioDriverTest, OutputGetUniqueId) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestUniqueId();
}

// AUDIO_STREAM_CMD_GET_STRING - Manufacturer
// For input stream, verify a valid GET_STRING (MANUFACTURER) response is successfully received.
TEST_F(AudioDriverTest, InputGetManufacturer) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestManufacturerString();
}

// For output stream, verify a valid GET_STRING (MANUFACTURER) response is successfully received.
TEST_F(AudioDriverTest, OutputGetManufacturer) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestManufacturerString();
}

// AUDIO_STREAM_CMD_GET_STRING - Product
// For input stream, verify a valid GET_STRING (PRODUCT) response is successfully received.
TEST_F(AudioDriverTest, InputGetProduct) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestProductString();
}

// For output stream, verify a valid GET_STRING (PRODUCT) response is successfully received.
TEST_F(AudioDriverTest, OutputGetProduct) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestProductString();
}

// AUDIO_STREAM_CMD_GET_GAIN
// For input stream, verify a valid GET_GAIN response is successfully received.
TEST_F(AudioDriverTest, InputGetGain) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestGain();
}

// For output stream, verify a valid GET_GAIN response is successfully received.
TEST_F(AudioDriverTest, OutputGetGain) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestGain();
}

// AUDIO_STREAM_CMD_SET_GAIN
// For input stream, verify a valid SET_GAIN response is successfully received.
TEST_F(AudioDriverTest, InputSetGain) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestGain();
  RequestSetGain();
}

// For output stream, verify a valid SET_GAIN response is successfully received.
TEST_F(AudioDriverTest, OutputSetGain) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestGain();
  RequestSetGain();
}

// AUDIO_STREAM_CMD_GET_FORMATS
// For input stream, verify a valid GET_FORMATS response is successfully received.
TEST_F(AudioDriverTest, InputGetFormats) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
}

// For output stream, verify a valid GET_FORMATS response is successfully received.
TEST_F(AudioDriverTest, OutputGetFormats) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
}

// AUDIO_STREAM_CMD_SET_FORMAT
// For output stream, verify a valid SET_FORMAT response, for low-bit-rate format -- and that a
// valid ring buffer channel is received.
TEST_F(AudioDriverTest, InputSetFormatMin) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();

  RequestSetFormatMin();
}

// For output stream, verify a valid SET_FORMAT response, for low-bit-rate format -- and that a
// valid ring buffer channel is received.
TEST_F(AudioDriverTest, OutputSetFormatMin) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();

  RequestSetFormatMin();
}

// For input stream, verify a valid SET_FORMAT response, for high-bit-rate format -- and that a
// valid ring buffer channel is received.
TEST_F(AudioDriverTest, InputSetFormatMax) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();

  RequestSetFormatMax();
}

// For output stream, verify a valid SET_FORMAT response, for high-bit-rate format -- and that a
// valid ring buffer channel is received.
TEST_F(AudioDriverTest, OutputSetFormatMax) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();

  RequestSetFormatMax();
}

// AUDIO_STREAM_CMD_PLUG_DETECT
// For input stream, verify a valid PLUG_DETECT response is successfully received.
TEST_F(AudioDriverTest, InputPlugDetect) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestPlugDetect();
}

// For output stream, verify a valid PLUG_DETECT response is successfully received.
TEST_F(AudioDriverTest, OutputPlugDetect) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestPlugDetect();
}

// AUDIO_STREAM_PLUG_DETECT_NOTIFY is not testable without scriptable PLUG/UNPLUG actions

// Ring Buffer channel commands
//
// AUDIO_RB_CMD_GET_FIFO_DEPTH
// For input stream, verify a valid GET_FIFO_DEPTH response is successfully received.
TEST_F(AudioDriverTest, InputGetFifoDepth) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMin();

  RequestFifoDepth();
}

// For output stream, verify a valid GET_FIFO_DEPTH response is successfully received.
TEST_F(AudioDriverTest, OutputGetFifoDepth) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();

  RequestFifoDepth();
}

// AUDIO_RB_CMD_GET_BUFFER
// For input stream, verify a GET_BUFFER response and ring buffer VMO is successfully received.
TEST_F(AudioDriverTest, InputGetBuffer) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();

  uint32_t frames = 48000;
  uint32_t notifs = 8;
  RequestBuffer(frames, notifs);
}

// For output stream, verify a GET_BUFFER response and ring buffer VMO is successfully received.
TEST_F(AudioDriverTest, OutputGetBuffer) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMin();

  uint32_t frames = 100;
  uint32_t notifs = 1;
  RequestBuffer(frames, notifs);
}

// AUDIO_RB_CMD_START
// For input stream, verify that a valid START response is successfully received.
TEST_F(AudioDriverTest, InputStart) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(100, 0);

  RequestStart();
}

// For output stream, verify that a valid START response is successfully received.
TEST_F(AudioDriverTest, OutputStart) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMin();
  RequestBuffer(32000, 0);

  RequestStart();
}

// AUDIO_RB_CMD_STOP
// For input stream, verify that a valid STOP response is successfully received.
TEST_F(AudioDriverTest, InputStop) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(24000, 0);
  RequestStart();

  RequestStop();
}

// For output stream, verify that a valid STOP response is successfully received.
TEST_F(AudioDriverTest, OutputStop) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMin();
  RequestBuffer(100, 0);
  RequestStart();

  RequestStop();
}

// AUDIO_RB_POSITION_NOTIFY
// For input stream, verify position notifications at fast rate (~180/sec) over approx 100 ms.
TEST_F(AudioDriverTest, InputPositionNotifyFast) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 32);
  RequestStart();

  ExpectPositionNotifyCount(16);
}

// For output stream, verify position notifications at fast rate (~180/sec) over approx 100 ms.
TEST_F(AudioDriverTest, OutputPositionNotifyFast) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 32);
  RequestStart();

  ExpectPositionNotifyCount(16);
}

// For input stream, verify position notifications at slow rate (2/sec) over approx 1 second.
TEST_F(AudioDriverTest, InputPositionNotifySlow) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMin();
  RequestBuffer(48000, 2);
  RequestStart();

  ExpectPositionNotifyCount(2);
}

// For output stream, verify position notifications at slow rate (2/sec) over approx 1 second.
TEST_F(AudioDriverTest, OutputPositionNotifySlow) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMin();
  RequestBuffer(48000, 2);
  RequestStart();

  ExpectPositionNotifyCount(2);
}

// For input stream, verify that no position notifications arrive if notifications_per_ring is 0.
TEST_F(AudioDriverTest, InputPositionNotifyNone) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 0);
  RequestStart();

  ExpectNoPositionNotifications();
}

// For output stream, verify that no position notifications arrive if notifications_per_ring is 0.
TEST_F(AudioDriverTest, OutputPositionNotifyNone) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 0);
  RequestStart();

  ExpectNoPositionNotifications();
}

// For input stream, verify that no position notificatons arrive after STOP.
TEST_F(AudioDriverTest, InputNoPositionNotifyAfterStop) {
  if (!WaitForDevice(DeviceType::Input)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 32);
  RequestStart();
  ExpectPositionNotifyCount(2);
  RequestStop();

  ExpectNoPositionNotifications();
}

// For output stream, verify that no position notificatons arrive after STOP.
TEST_F(AudioDriverTest, OutputNoPositionNotifyAfterStop) {
  if (!WaitForDevice(DeviceType::Output)) {
    return;
  }

  RequestFormats();
  RequestSetFormatMax();
  RequestBuffer(8000, 32);
  RequestStart();
  ExpectPositionNotifyCount(2);
  RequestStop();

  ExpectNoPositionNotifications();
}

// For input stream, verify that monotonic_time values are close to NOW, and always increasing.

}  // namespace media::audio::test
