// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/test_base.h"

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/fdio/fdio.h>

#include <algorithm>
#include <cstring>
#include <string>

#include <fbl/unique_fd.h>

#include "lib/zx/time.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::drivers::test {

// Device discovery is done once at binary open; a fresh FIDL channel is used for each test.
void TestBase::SetUp() {
  media::audio::test::TestFixture::SetUp();

  ConnectToDevice(device_entry());
}

void TestBase::TearDown() {
  stream_config_.Unbind();

  // Audio drivers can have multiple StreamConfig channels open, but only one can be 'privileged':
  // the one that can in turn create a RingBuffer channel. Each test case starts from scratch,
  // opening and closing channels. If we create a StreamConfig channel before the previous one is
  // cleared, a new StreamConfig channel will not be privileged and Admin tests fail.
  //
  // When disconnecting a StreamConfig, there's no signal to wait on before proceeding (potentially
  // immediately executing other tests); insert a 10-msec wait (>3.5 was never observed).
  zx::nanosleep(zx::deadline_after(zx::msec(10)));

  TestFixture::TearDown();
}

// Given this device_entry, open the device and set the FIDL config_channel
void TestBase::ConnectToDevice(const DeviceEntry& device_entry) {
  // Open the device node.
  fbl::unique_fd dev_node(openat(device_entry.dir_fd, device_entry.filename.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FAIL() << "AudioDriver::TestBase failed to open device node at \"" << device_entry.filename
           << "\". (" << strerror(errno) << " : " << errno << ")";
  }

  // Obtain the FDIO device channel, wrap it in a sync proxy, use that to get the stream channel.
  zx::channel dev_channel;
  zx_status_t status =
      fdio_get_service_handle(dev_node.release(), dev_channel.reset_and_get_address());
  if (status != ZX_OK) {
    FAIL() << status << "Err " << status << ", failed to obtain FDIO service channel to audio "
           << ((device_type() == DeviceType::Input) ? "input" : "output");
  }

  // Obtain the stream channel
  auto dev =
      fidl::InterfaceHandle<fuchsia::hardware::audio::Device>(std::move(dev_channel)).BindSync();
  fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config_handle = {};

  status = dev->GetChannel(&stream_config_handle);
  if (status != ZX_OK) {
    FAIL() << status << "Err " << status << ", failed to open channel to audio "
           << (device_type() == DeviceType::Input ? "input" : "output");
  }

  auto channel = stream_config_handle.TakeChannel();
  FX_LOGS(TRACE) << "Successfully opened devnode '" << device_entry.filename << "' for audio "
                 << ((device_type() == DeviceType::Input) ? "input" : "output");

  stream_config_ =
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig>(std::move(channel)).Bind();
  // If no device was enumerated, don't waste further time.
  if (!stream_config_.is_bound()) {
    FAIL() << "Failed to get stream channel for this device";
  }
  stream_config_.set_error_handler([this](zx_status_t status) {
    set_failed();
    FAIL() << "StreamConfig channel error " << status;
  });
}

// Request that the driver return the format ranges that it supports.
void TestBase::RequestFormats() {
  stream_config()->GetSupportedFormats(
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
