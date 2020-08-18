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
    {.path = "/dev/class/audio-input-2", .device_type = DeviceType::Input},
    {.path = "/dev/class/audio-output-2", .device_type = DeviceType::Output},
};

// static
bool TestBase::test_admin_functions_ = false;

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
          AUDIO_LOG(DEBUG) << "'" << filename << "' dir_fd " << dir_fd;
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

  // Assert that we have a valid channel to communicate with the driver at all.
  ASSERT_FALSE(stream_channels_.empty());
  ASSERT_TRUE(stream_channels_.back()->is_valid());
  stream_channels_.pop_back();
}

void TestBase::TearDown() {
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

  auto channel = stream_config.TakeChannel();
  stream_channels_.push_back(zx::unowned_channel(channel.get()));
  AUDIO_LOG(DEBUG) << "Successfully opened devnode '" << name << "' for audio "
                   << ((device_type_ == DeviceType::Input) ? "input" : "output");

  stream_config_ =
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig>(std::move(channel)).Bind();
  if (!stream_config_.is_bound()) {
    ADD_FAILURE() << "Failed to get stream channel";
  }
  stream_config_.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE() << "Stream config channel error " << status; });

  stream_config_ready_ = true;
}

// Request that the driver return the format ranges that it supports.
void TestBase::RequestFormats() {
  stream_config_->GetSupportedFormats(
      [this](std::vector<fuchsia::hardware::audio::SupportedFormats> supported_formats) {
        EXPECT_GT(supported_formats.size(), 0u);

        for (size_t i = 0; i < supported_formats.size(); ++i) {
          auto& format = supported_formats[i].pcm_supported_formats();

          uint8_t largest_bytes_per_sample = 0;
          EXPECT_NE(format.bytes_per_sample.size(), 0u);
          for (size_t j = 0; j < format.bytes_per_sample.size(); ++j) {
            EXPECT_NE(format.bytes_per_sample[j], 0u);
            if (format.bytes_per_sample[j] > largest_bytes_per_sample) {
              largest_bytes_per_sample = format.bytes_per_sample[j];
            }
          }
          for (size_t j = 0; j < format.valid_bits_per_sample.size(); ++j) {
            EXPECT_LE(format.valid_bits_per_sample[j], largest_bytes_per_sample * 8);
          }

          EXPECT_NE(format.frame_rates.size(), 0u);
          for (size_t j = 0; j < format.frame_rates.size(); ++j) {
            EXPECT_GE(format.frame_rates[j], fuchsia::media::MIN_PCM_FRAMES_PER_SECOND);
            EXPECT_LE(format.frame_rates[j], fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
          }

          EXPECT_NE(format.number_of_channels.size(), 0u);
          for (size_t j = 0; j < format.number_of_channels.size(); ++j) {
            EXPECT_GE(format.number_of_channels[j], fuchsia::media::MIN_PCM_CHANNEL_COUNT);
            EXPECT_LE(format.number_of_channels[j], fuchsia::media::MAX_PCM_CHANNEL_COUNT);
          }

          pcm_formats_.push_back(format);
        }

        received_get_formats_ = true;
      });
  RunLoopUntil([this]() { return received_get_formats_; });
}

}  // namespace media::audio::drivers::test
