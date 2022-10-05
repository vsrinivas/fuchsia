// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/drivers/test/test_base.h"

#include <fcntl.h>
#include <fuchsia/component/cpp/fidl.h>
#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <algorithm>
#include <cstring>
#include <string>

#include <fbl/unique_fd.h>

#include "gtest/gtest.h"
#include "src/media/audio/drivers/test/audio_device_enumerator_stub.h"

namespace media::audio::drivers::test {

using component_testing::ChildRef;
using component_testing::ParentRef;
using component_testing::Protocol;
using component_testing::RealmBuilder;
using component_testing::RealmRoot;
using component_testing::Route;

// Device discovery is done once at binary open; a fresh FIDL channel is used for each test.
void TestBase::SetUp() {
  media::audio::test::TestFixture::SetUp();

  if (device_entry().dir_fd == DeviceEntry::kA2dp) {
    ConnectToBluetoothDevice();
  } else {
    ConnectToDevice(device_entry());
  }
}

void TestBase::TearDown() {
  stream_config_.Unbind();

  // Audio drivers can have multiple StreamConfig channels open, but only one can be 'privileged':
  // the one that can in turn create a RingBuffer channel. Each test case starts from scratch,
  // opening and closing channels. If we create a StreamConfig channel before the previous one is
  // cleared, a new StreamConfig channel will not be privileged and Admin tests will fail.
  //
  // When disconnecting a StreamConfig, there's no signal to wait on before proceeding (potentially
  // immediately executing other tests); insert a 10-ms wait (needing >3.5ms was never observed).
  zx::nanosleep(zx::deadline_after(zx::msec(10)));

  TestFixture::TearDown();
}

void TestBase::ConnectToBluetoothDevice() {
  std::unique_ptr<AudioDeviceEnumeratorStub> audio_device_enumerator_impl =
      std::make_unique<AudioDeviceEnumeratorStub>();
  auto audio_device_enumerator_impl_ptr = audio_device_enumerator_impl.get();

  auto builder = RealmBuilder::Create();
  // The component binding must live as long as the Realm, so std::move the
  // unique_ptr into the component function.
  builder.AddLocalChild(
      "audio-device-enumerator",
      [audio_device_enumerator_impl = std::move(audio_device_enumerator_impl)]() mutable {
        // Note: This lambda does not create a new instance,
        // so the component can only be started once.
        return std::move(audio_device_enumerator_impl);
      });
  builder.AddChild("audio-device-output-harness", "#meta/audio-device-output-harness.cm");
  builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::media::AudioDeviceEnumerator::Name_}},
                         .source = ChildRef{"audio-device-enumerator"},
                         .targets = {ChildRef{"audio-device-output-harness"}}});
  builder.AddRoute(Route{.capabilities = {Protocol{fuchsia::logger::LogSink::Name_}},
                         .source = ParentRef{},
                         .targets = {ChildRef{"audio-device-output-harness"}}});
  builder.AddRoute(Route{
      .capabilities = {Protocol{.name = fuchsia::component::Binder::Name_, .as = "audio-binder"}},
      .source = ChildRef{"audio-device-output-harness"},
      .targets = {ParentRef{}}});
  realm_ = std::make_unique<RealmRoot>(builder.Build());
  ASSERT_EQ(ZX_OK, realm_->Connect("audio-binder", audio_binder_.NewRequest().TakeChannel()));
  audio_binder_.set_error_handler(
      [](zx_status_t status) { FAIL() << "audio-device-output-harness exited"; });

  // Wait for the Bluetooth harness to AddDeviceByChannel, then pass it on
  RunLoopUntil([impl = audio_device_enumerator_impl_ptr]() {
    return impl->channel_available() || HasFailure();
  });
  CreateStreamConfigFromChannel(audio_device_enumerator_impl_ptr->TakeChannel());
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
  auto device =
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfigConnector>(std::move(dev_channel))
          .Bind();
  device.set_error_handler([this](zx_status_t status) {
    FAIL() << status << "Err " << status << ", failed to open channel to audio "
           << (device_type() == DeviceType::Input ? "input" : "output");
  });
  fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> stream_config_client;
  fidl::InterfaceRequest<fuchsia::hardware::audio::StreamConfig> stream_config_server =
      stream_config_client.NewRequest();
  device->Connect(std::move(stream_config_server));

  auto channel = stream_config_client.TakeChannel();
  FX_LOGS(TRACE) << "Successfully opened devnode '" << device_entry.filename << "' for audio "
                 << ((device_type() == DeviceType::Input) ? "input" : "output");

  CreateStreamConfigFromChannel(
      fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig>(std::move(channel)));
}

void TestBase::CreateStreamConfigFromChannel(
    fidl::InterfaceHandle<fuchsia::hardware::audio::StreamConfig> channel) {
  stream_config_ = channel.Bind();

  // If no device was enumerated, don't waste further time.
  if (!stream_config_.is_bound()) {
    FAIL() << "Failed to get stream channel for this device";
  }
  AddErrorHandler(stream_config_, "StreamConfig");
}

// Request that the driver return the format ranges that it supports.
void TestBase::RequestFormats() {
  bool received_formats = false;
  stream_config()->GetSupportedFormats(
      AddCallback("GetSupportedFormats",
                  [this, &received_formats](
                      std::vector<fuchsia::hardware::audio::SupportedFormats> supported_formats) {
                    EXPECT_FALSE(supported_formats.empty());

                    for (size_t i = 0; i < supported_formats.size(); ++i) {
                      ASSERT_TRUE(supported_formats[i].has_pcm_supported_formats());
                      auto& format_set = *supported_formats[i].mutable_pcm_supported_formats();
                      pcm_formats_.push_back(std::move(format_set));
                    }

                    received_formats = true;
                  }));
  ExpectCallbacks();
  if (!HasFailure()) {
    ValidateGetFormats();
  }
  if (!HasFailure()) {
    SetMinMaxFormats();
  }
}

void TestBase::LogFormat(const fuchsia::hardware::audio::PcmFormat& format, std::string tag) {
  FX_LOGS(WARNING) << tag << ": rate " << format.frame_rate << ", fmt "
                   << static_cast<int>(format.sample_format) << ", " << format.bytes_per_sample * 8u
                   << "b (" << static_cast<uint16_t>(format.valid_bits_per_sample)
                   << " valid), chans " << static_cast<uint16_t>(format.number_of_channels);
}

void TestBase::ValidateGetFormats() {
  for (size_t i = 0; i < pcm_formats_.size(); ++i) {
    SCOPED_TRACE(testing::Message() << "pcm_format[" << i << "]");
    auto& format_set = pcm_formats_[i];

    ASSERT_TRUE(format_set.has_channel_sets());
    ASSERT_TRUE(format_set.has_sample_formats());
    ASSERT_TRUE(format_set.has_bytes_per_sample());
    ASSERT_TRUE(format_set.has_valid_bits_per_sample());
    ASSERT_TRUE(format_set.has_frame_rates());

    ASSERT_FALSE(format_set.channel_sets().empty());
    ASSERT_FALSE(format_set.sample_formats().empty());
    ASSERT_FALSE(format_set.bytes_per_sample().empty());
    ASSERT_FALSE(format_set.valid_bits_per_sample().empty());
    ASSERT_FALSE(format_set.frame_rates().empty());

    EXPECT_LE(format_set.channel_sets().size(), fuchsia::hardware::audio::MAX_COUNT_CHANNEL_SETS);
    EXPECT_LE(format_set.sample_formats().size(),
              fuchsia::hardware::audio::MAX_COUNT_SUPPORTED_SAMPLE_FORMATS);
    EXPECT_LE(format_set.bytes_per_sample().size(),
              fuchsia::hardware::audio::MAX_COUNT_SUPPORTED_BYTES_PER_SAMPLE);
    EXPECT_LE(format_set.valid_bits_per_sample().size(),
              fuchsia::hardware::audio::MAX_COUNT_SUPPORTED_VALID_BITS_PER_SAMPLE);
    EXPECT_LE(format_set.frame_rates().size(), fuchsia::hardware::audio::MAX_COUNT_SUPPORTED_RATES);

    for (size_t j = 0; j < format_set.channel_sets().size(); ++j) {
      SCOPED_TRACE(testing::Message() << "channel_set[" << j << "]");
      auto& channel_set = format_set.channel_sets()[j];

      ASSERT_TRUE(channel_set.has_attributes());
      ASSERT_FALSE(channel_set.attributes().empty());
      EXPECT_LE(channel_set.attributes().size(),
                fuchsia::hardware::audio::MAX_COUNT_CHANNELS_IN_RING_BUFFER);

      for (size_t k = 0; k < channel_set.attributes().size(); ++k) {
        SCOPED_TRACE(testing::Message() << "attributes[" << k << "]");
        auto& attribs = channel_set.attributes()[k];

        if (attribs.has_min_frequency()) {
          EXPECT_LT(attribs.min_frequency(), fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
        }
        if (attribs.has_max_frequency()) {
          EXPECT_GT(attribs.max_frequency(), fuchsia::media::MIN_PCM_FRAMES_PER_SECOND);
          EXPECT_LE(attribs.max_frequency(), fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
          if (attribs.has_min_frequency()) {
            EXPECT_LE(attribs.min_frequency(), attribs.max_frequency());
          }
        }
      }
    }

    for (size_t j = 0; j < format_set.frame_rates().size(); ++j) {
      SCOPED_TRACE(testing::Message() << "frame_rates[" << j << "]");

      EXPECT_GE(format_set.frame_rates()[j], fuchsia::media::MIN_PCM_FRAMES_PER_SECOND);
      EXPECT_LE(format_set.frame_rates()[j], fuchsia::media::MAX_PCM_FRAMES_PER_SECOND);
    }
  }
}

void TestBase::SetMinMaxFormats() {
  for (size_t i = 0; i < pcm_formats_.size(); ++i) {
    SCOPED_TRACE(testing::Message() << "pcm_format[" << i << "]");
    size_t min_chans, max_chans;
    uint8_t min_bytes_per_sample, max_bytes_per_sample;
    uint8_t min_valid_bits_per_sample, max_valid_bits_per_sample;
    uint32_t min_frame_rate, max_frame_rate;

    auto& format_set = pcm_formats_[i];
    fuchsia::hardware::audio::SampleFormat sample_format = format_set.sample_formats()[0];

    for (size_t j = 0; j < format_set.channel_sets().size(); ++j) {
      if (j == 0 || format_set.channel_sets()[j].attributes().size() < min_chans) {
        min_chans = format_set.channel_sets()[j].attributes().size();
      }
      if (j == 0 || format_set.channel_sets()[j].attributes().size() > max_chans) {
        max_chans = format_set.channel_sets()[j].attributes().size();
      }
    }

    for (size_t j = 0; j < format_set.bytes_per_sample().size(); ++j) {
      SCOPED_TRACE(testing::Message() << "bytes_per_sample[" << j << "]");
      EXPECT_GT(format_set.bytes_per_sample()[j], 0u);

      if (j == 0 || format_set.bytes_per_sample()[j] < min_bytes_per_sample) {
        min_bytes_per_sample = format_set.bytes_per_sample()[j];
      }
      if (j == 0 || format_set.bytes_per_sample()[j] > max_bytes_per_sample) {
        max_bytes_per_sample = format_set.bytes_per_sample()[j];
      }
    }

    for (size_t j = 0; j < format_set.valid_bits_per_sample().size(); ++j) {
      SCOPED_TRACE(testing::Message() << "valid_bits_per_sample[" << j << "]");
      EXPECT_LE(format_set.valid_bits_per_sample()[j], max_bytes_per_sample * 8);
      EXPECT_GT(format_set.valid_bits_per_sample()[j], 0u);

      if (j == 0 || format_set.valid_bits_per_sample()[j] < min_valid_bits_per_sample) {
        min_valid_bits_per_sample = format_set.valid_bits_per_sample()[j];
      }
      if (j == 0 || format_set.valid_bits_per_sample()[j] > max_valid_bits_per_sample) {
        max_valid_bits_per_sample = format_set.valid_bits_per_sample()[j];
      }
    }
    EXPECT_LE(min_valid_bits_per_sample, min_bytes_per_sample * 8);
    EXPECT_LE(max_valid_bits_per_sample, max_bytes_per_sample * 8);

    for (size_t j = 0; j < format_set.frame_rates().size(); ++j) {
      if (j == 0 || format_set.frame_rates()[j] < min_frame_rate) {
        min_frame_rate = format_set.frame_rates()[j];
      }
      if (j == 0 || format_set.frame_rates()[j] > max_frame_rate) {
        max_frame_rate = format_set.frame_rates()[j];
      }
    }

    // save, if less than min
    auto bit_rate = min_chans * min_bytes_per_sample * min_frame_rate;
    if (i == 0 || bit_rate < min_format_.number_of_channels * min_format_.bytes_per_sample *
                                 min_format_.frame_rate) {
      min_format_ = {
          .number_of_channels = static_cast<uint8_t>(min_chans),
          .sample_format = sample_format,
          .bytes_per_sample = min_bytes_per_sample,
          .valid_bits_per_sample = min_valid_bits_per_sample,
          .frame_rate = min_frame_rate,
      };
    }
    // save, if more than max
    bit_rate = max_chans * max_bytes_per_sample * max_frame_rate;
    if (i == 0 || bit_rate > max_format_.number_of_channels * max_format_.bytes_per_sample *
                                 max_format_.frame_rate) {
      max_format_ = {
          .number_of_channels = static_cast<uint8_t>(max_chans),
          .sample_format = sample_format,
          .bytes_per_sample = max_bytes_per_sample,
          .valid_bits_per_sample = max_valid_bits_per_sample,
          .frame_rate = max_frame_rate,
      };
    }
  }
}

}  // namespace media::audio::drivers::test
