// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/test_base.h"

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/fdio/fdio.h>

#include <algorithm>
#include <cstring>

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::drivers::test {

static const struct {
  const char* path;
  DeviceType device_type;
} AUDIO_DEVNODES[] = {
    {.path = "/dev/class/audio-input", .device_type = DeviceType::Input},
    {.path = "/dev/class/audio-output", .device_type = DeviceType::Output},
};

// static
bool TestBase::test_admin_functions_ = false;

uint32_t TestBase::unique_transaction_id_ = 0;

zx_txid_t TestBase::NextTransactionId() {
  uint32_t trans_id = ++unique_transaction_id_;
  if (trans_id == AUDIO_INVALID_TRANSACTION_ID) {
    trans_id = ++unique_transaction_id_;
  }
  return trans_id;
}

bool TestBase::no_devices_found_[2] = {false, false};

// Device discovery is performed at start of every test case. If a test sets no_devices_found_, any
// remaining tests in that suite are automatically skipped. By clearing these, we re-run device
// discovery for every test suite, even if previously no devices were found at some point.
void TestBase::SetUpTestSuite() {
  no_devices_found_[DeviceType::Input] = no_devices_found_[DeviceType::Output] = false;
}

std::string TestBase::DeviceTypeToString(const testing::TestParamInfo<TestBase::ParamType>& info) {
  return (info.param == DeviceType::Input ? "Input" : "Output");
};

// The test parameter indicates whether we are testing an Input or an Output device.
void TestBase::SetUp() {
  TestFixture::SetUp();

  device_type_ = GetParam();
}

void TestBase::EnumerateDevices() {
  stream_channels_.clear();

  bool enumeration_done = false;

  // Set up the watchers, etc. If any fail, automatically stop monitoring all device sources.
  for (const auto& devnode : AUDIO_DEVNODES) {
    if (device_type_ != devnode.device_type) {
      continue;
    }

    auto watcher = fsl::DeviceWatcher::CreateWithIdleCallback(
        devnode.path,
        [this](int dir_fd, const std::string& filename) {
          AUD_VLOG(TRACE) << "'" << filename << "' dir_fd " << dir_fd;
          this->AddDevice(dir_fd, filename);
        },
        [&enumeration_done]() { enumeration_done = true; });

    if (watcher == nullptr) {
      watchers_.clear();
      ASSERT_FALSE(watcher == nullptr)
          << "AudioDriver::TestBase failed creating DeviceWatcher for '" << devnode.path << "'.";
    }
    watchers_.emplace_back(std::move(watcher));
  }
  //
  // ... or ...
  //
  // Receive a call to AddDeviceByChannel(std::move(stream_channel), name);
  //

  RunLoopUntil([&enumeration_done]() { return enumeration_done; });

  // If we timed out waiting for devices, this target may not have any. Don't waste further time.
  if (stream_channels_.empty()) {
    TestBase::no_devices_found_[device_type_] = true;
    FX_LOGS(WARNING) << "*** No audio "
                     << ((device_type_ == DeviceType::Input) ? "input" : "output")
                     << " devices detected on this target. ***";
    GTEST_SKIP();
    __UNREACHABLE;
  }

  // Assert that we can communicate with the driver at all.
  ASSERT_FALSE(stream_channels_.empty());
  zx_status_t status = stream_transceiver_.Init(
      std::move(stream_channels_.back()), fit::bind_member(this, &TestBase::OnInboundStreamMessage),
      ErrorHandler());
  EXPECT_EQ(ZX_OK, status);
  stream_channels_.pop_back();
  ASSERT_TRUE(stream_transceiver_.channel().is_valid());
}

void TestBase::TearDown() {
  stream_transceiver_.Close();
  watchers_.clear();

  TestFixture::TearDown();
}

void TestBase::AddDevice(int dir_fd, const std::string& name) {
  // TODO(mpuryear): on systems with more than one audio device of a given type, test them all.
  if (!stream_channels_.empty()) {
    FX_LOGS(WARNING) << "More than one device detected. We test only the most-recently-added";
  }

  // Open the device node.
  fbl::unique_fd dev_node(openat(dir_fd, name.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FX_LOGS(ERROR) << "AudioDriver::TestBase failed to open device node at \"" << name << "\". ("
                   << strerror(errno) << " : " << errno << ")";
    FAIL();
  }

  // Obtain the FDIO device channel, wrap it in a sync proxy, use that to get the stream channel.
  zx::channel dev_channel;
  zx_status_t status =
      fdio_get_service_handle(dev_node.release(), dev_channel.reset_and_get_address());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to obtain FDIO service channel to audio "
                            << ((device_type_ == DeviceType::Input) ? "input" : "output");
    FAIL();
  }

  // Obtain the stream channel
  auto dev =
      fidl::InterfaceHandle<fuchsia::hardware::audio::Device>(std::move(dev_channel)).BindSync();
  fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config = {};

  status = dev->GetChannel(&stream_config);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to open channel to audio "
                            << ((device_type_ == DeviceType::Input) ? "input" : "output");
    FAIL();
  }

  stream_channels_.push_back(stream_config.TakeChannel());
  AUD_VLOG(TRACE) << "Successfully opened devnode '" << name << "' for audio "
                  << ((device_type_ == DeviceType::Input) ? "input" : "output");
}

// Stream channel requests
//
// Request that the driver return the format ranges that it supports.
void TestBase::RequestFormats() {
  if (error_occurred_) {
    return;
  }

  media::audio::test::MessageTransceiver::Message request_message;
  auto& request = request_message.ResizeBytesAs<audio_stream_cmd_get_formats_req_t>();
  get_formats_transaction_id_ = NextTransactionId();
  request.hdr.transaction_id = get_formats_transaction_id_;
  request.hdr.cmd = AUDIO_STREAM_CMD_GET_FORMATS;

  EXPECT_EQ(ZX_OK, stream_transceiver().SendMessage(request_message));

  RunLoopUntil([this]() { return received_get_formats_ || error_occurred_; });
}

// Handle an incoming stream channel message (generally a response from a previous request)
void TestBase::OnInboundStreamMessage(media::audio::test::MessageTransceiver::Message message) {
  // This method is overloaded by AudioDriverAdminTest, to handle AUDIO_STREAM_CMD_SET_FORMAT
  HandleInboundStreamMessage(message);
}

// Validate just the command portion of the response header.
bool TestBase::ValidateResponseCommand(audio_cmd_hdr header, audio_cmd_t expected_command) {
  EXPECT_EQ(header.cmd, expected_command) << "Unexpected command!";

  return (expected_command == header.cmd);
}

// Validate just the transaction ID portion of the response header.
void TestBase::ValidateResponseTransaction(audio_cmd_hdr header,
                                           zx_txid_t expected_transaction_id) {
  EXPECT_EQ(header.transaction_id, expected_transaction_id) << "Unexpected transaction ID!";
}

// Validate the entire response header.
bool TestBase::ValidateResponseHeader(audio_cmd_hdr header, zx_txid_t expected_transaction_id,
                                      audio_cmd_t expected_command) {
  ValidateResponseTransaction(header, expected_transaction_id);
  return ValidateResponseCommand(header, expected_command);
}

// Handle a get_formats response on the stream channel. This response may be a multi-part.
void TestBase::HandleGetFormatsResponse(const audio_stream_cmd_get_formats_resp_t& response) {
  if (!ValidateResponseHeader(response.hdr, get_formats_transaction_id_,
                              AUDIO_STREAM_CMD_GET_FORMATS)) {
    return;
  }

  EXPECT_GT(response.format_range_count, 0u);
  EXPECT_LT(response.first_format_range_ndx, response.format_range_count);
  EXPECT_EQ(response.first_format_range_ndx, next_format_range_ndx_);
  EXPECT_EQ(response.first_format_range_ndx % AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE,
            0u)
      << "First format index of each get_formats response must be a multiple of "
      << AUDIO_STREAM_CMD_GET_FORMATS_MAX_RANGES_PER_RESPONSE;

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

}  // namespace media::audio::drivers::test
