// Copyright 2022 The Fuchsia Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-stream.h"

#include <fuchsia/hardware/intelhda/codec/cpp/banjo.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl/cpp/wire/connect_service.h>

#include <optional>
#include <thread>
#include <vector>

#include <ddktl/device.h>
#include <intel-hda/codec-utils/codec-driver-base.h>
#include <intel-hda/codec-utils/streamconfig-base.h>
#include <zxtest/zxtest.h>

namespace {

static constexpr char kTestProductName[] = "Builtin Headphone Jack";

fidl::WireSyncClient<fuchsia_hardware_audio::Dai> GetDaiClient(
    fidl::ClientEnd<fuchsia_hardware_audio::DaiConnector> client) {
  fidl::WireSyncClient client_wrap{std::move(client)};
  if (!client_wrap.is_valid()) {
    return {};
  }
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_audio::Dai>();
  if (!endpoints.is_ok()) {
    return {};
  }
  auto [stream_channel_local, stream_channel_remote] = *std::move(endpoints);
  ZX_ASSERT(client_wrap->Connect(std::move(stream_channel_remote)).ok());
  return fidl::WireSyncClient<fuchsia_hardware_audio::Dai>(std::move(stream_channel_local));
}

}  // namespace

namespace audio::intel_hda {

class TestCodec : public codecs::IntelHDACodecDriverBase {
 public:
  explicit TestCodec() = default;

  zx_status_t ActivateStream(const fbl::RefPtr<codecs::IntelHDAStreamBase>& stream) {
    return codecs::IntelHDACodecDriverBase::ActivateStream(stream);
  }

  zx::result<> Bind(zx_device_t* codec_dev, const char* name) {
    return codecs::IntelHDACodecDriverBase::Bind(codec_dev, name);
  }
  void DeviceRelease() { codecs::IntelHDACodecDriverBase::DeviceRelease(); }
};

class TestStream : public IntelDspStream {
 public:
  TestStream()
      : IntelDspStream(DspStream{.id = DspPipelineId{1},
                                 .host_format = kTestHostFormat,
                                 .dai_format = kTestDaiFormat,
                                 .is_i2s = true,
                                 .stream_id = 3,
                                 .is_input = false,
                                 .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_HEADPHONE_JACK,
                                 .name = kTestProductName}) {}
  zx_status_t Bind() {
    fbl::AutoLock lock(obj_lock());
    return PublishDeviceLocked();
  }

 private:
  static constexpr AudioDataFormat kTestHostFormat = {
      .sampling_frequency = SamplingFrequency::FS_48000HZ,
      .bit_depth = BitDepth::DEPTH_16BIT,
      .channel_map = 0xFFFFFF10,
      .channel_config = ChannelConfig::CONFIG_STEREO,
      .interleaving_style = InterleavingStyle::PER_CHANNEL,
      .number_of_channels = 2,
      .valid_bit_depth = 16,
      .sample_type = SampleType::INT_MSB,
      .reserved = 0,
  };
  static constexpr AudioDataFormat kTestDaiFormat = {
      .sampling_frequency = SamplingFrequency::FS_48000HZ,
      .bit_depth = BitDepth::DEPTH_32BIT,
      .channel_map = 0xFFFFFF10,
      .channel_config = ChannelConfig::CONFIG_STEREO,
      .interleaving_style = InterleavingStyle::PER_CHANNEL,
      .number_of_channels = 2,
      .valid_bit_depth = 24,
      .sample_type = SampleType::INT_MSB,
      .reserved = 0,
  };
};

class FakeController;
using FakeControllerType = ddk::Device<FakeController>;

class FakeController : public FakeControllerType, public ddk::IhdaCodecProtocol<FakeController> {
 public:
  explicit FakeController(zx_device_t* parent)
      : FakeControllerType(parent), loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  ~FakeController() { loop_.Shutdown(); }
  zx_device_t* dev() { return reinterpret_cast<zx_device_t*>(this); }
  zx_status_t Bind() { return DdkAdd("fake-controller-device-test"); }
  void DdkRelease() {}

  ihda_codec_protocol_t proto() const {
    ihda_codec_protocol_t proto;
    proto.ctx = const_cast<FakeController*>(this);
    proto.ops = const_cast<ihda_codec_protocol_ops_t*>(&ihda_codec_protocol_ops_);
    return proto;
  }

  // ZX_PROTOCOL_IHDA_CODEC Interface
  zx_status_t IhdaCodecGetDriverChannel(zx::channel* out_channel) {
    zx::channel channel_local;
    zx::channel channel_remote;
    zx_status_t status = zx::channel::create(0, &channel_local, &channel_remote);
    if (status != ZX_OK) {
      return status;
    }

    fbl::RefPtr<Channel> channel = Channel::Create(std::move(channel_local));
    if (channel == nullptr) {
      return ZX_ERR_NO_MEMORY;
    }
    codec_driver_channel_ = channel;
    codec_driver_channel_->SetHandler([](async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                         zx_status_t status, const zx_packet_signal_t* signal) {});
    status = codec_driver_channel_->BeginWait(loop_.dispatcher());
    if (status != ZX_OK) {
      codec_driver_channel_.reset();
      return status;
    }

    if (status == ZX_OK) {
      *out_channel = std::move(channel_remote);
    }

    return status;
  }

 private:
  async::Loop loop_;
  fbl::RefPtr<Channel> codec_driver_channel_;
};

class Binder : public fake_ddk::Bind {
 public:
 private:
  zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                void* protocol) override {
    auto context = reinterpret_cast<const FakeController*>(device);
    if (proto_id == ZX_PROTOCOL_IHDA_CODEC) {
      *reinterpret_cast<ihda_codec_protocol_t*>(protocol) = context->proto();
      return ZX_OK;
    }
    return ZX_ERR_PROTOCOL_NOT_SUPPORTED;
  }
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status = fake_ddk::Bind::DeviceAdd(drv, parent, args, out);
    bad_parent_ = false;
    return status;
  }
};

class SstStreamTest : public zxtest::Test {
 public:
  SstStreamTest() : fake_controller_(fake_ddk::kFakeParent) {}

  void SetUp() override {
    ASSERT_OK(fake_controller_.Bind());
    codec_ = fbl::AdoptRef(new TestCodec);
    auto ret = codec_->Bind(fake_controller_.dev(), "test");
    ASSERT_OK(ret.status_value());
    stream_ = fbl::AdoptRef(new TestStream);
    ASSERT_OK(codec_->ActivateStream(stream_));
    ASSERT_OK(stream_->Bind());
  }

  void TearDown() override {
    codec_->DeviceRelease();
    fake_controller_.DdkAsyncRemove();
    EXPECT_TRUE(tester_.Ok());
    fake_controller_.DdkRelease();
  }

 protected:
  Binder tester_;
  FakeController fake_controller_;
  fbl::RefPtr<TestCodec> codec_;
  fbl::RefPtr<TestStream> stream_;
};

TEST_F(SstStreamTest, GetStreamProperties) {
  auto stream_client = GetDaiClient(tester_.FidlClient<fuchsia_hardware_audio::DaiConnector>());
  ASSERT_TRUE(stream_client.is_valid());

  auto result = stream_client->GetProperties();
  ASSERT_OK(result.status());

  const char* kManufacturer = "Intel";
  ASSERT_BYTES_EQ(result.value().properties.manufacturer().data(), kManufacturer,
                  strlen(kManufacturer));
  ASSERT_BYTES_EQ(result.value().properties.product_name().data(), kTestProductName,
                  strlen(kTestProductName));
  EXPECT_EQ(result.value().properties.is_input(), false);
}

TEST_F(SstStreamTest, Reset) {
  auto stream_client = GetDaiClient(tester_.FidlClient<fuchsia_hardware_audio::DaiConnector>());
  ASSERT_TRUE(stream_client.is_valid());

  auto result = stream_client->Reset();
  ASSERT_OK(result.status());
}

TEST_F(SstStreamTest, GetRingBufferFormats) {
  auto stream_client = GetDaiClient(tester_.FidlClient<fuchsia_hardware_audio::DaiConnector>());
  ASSERT_TRUE(stream_client.is_valid());

  auto result = stream_client->GetRingBufferFormats();
  ASSERT_OK(result.status());
  auto& formats = result.value().value()->ring_buffer_formats;
  EXPECT_EQ(formats.count(), 1);
  EXPECT_EQ(formats[0].pcm_supported_formats().channel_sets().count(), 1);
  EXPECT_EQ(formats[0].pcm_supported_formats().channel_sets()[0].attributes().count(), 2);
  EXPECT_EQ(formats[0].pcm_supported_formats().sample_formats().count(), 1);
  EXPECT_EQ(formats[0].pcm_supported_formats().sample_formats()[0],
            fuchsia_hardware_audio::wire::SampleFormat::kPcmSigned);
  EXPECT_EQ(formats[0].pcm_supported_formats().bytes_per_sample().count(), 1);
  EXPECT_EQ(formats[0].pcm_supported_formats().bytes_per_sample()[0], 2);
  EXPECT_EQ(formats[0].pcm_supported_formats().valid_bits_per_sample().count(), 1);
  EXPECT_EQ(formats[0].pcm_supported_formats().valid_bits_per_sample()[0], 16);
  EXPECT_EQ(formats[0].pcm_supported_formats().frame_rates().count(), 1);
  EXPECT_EQ(formats[0].pcm_supported_formats().frame_rates()[0], 48'000);
}

TEST_F(SstStreamTest, GetDaiFormats) {
  auto stream_client = GetDaiClient(tester_.FidlClient<fuchsia_hardware_audio::DaiConnector>());
  ASSERT_TRUE(stream_client.is_valid());

  auto result = stream_client->GetDaiFormats();
  ASSERT_OK(result.status());
  auto& formats = result.value().value()->dai_formats;
  EXPECT_EQ(formats.count(), 1);
  EXPECT_EQ(formats[0].number_of_channels.count(), 1);
  EXPECT_EQ(formats[0].number_of_channels[0], 2);  // I2S.
  EXPECT_EQ(formats[0].sample_formats.count(), 1);
  EXPECT_EQ(formats[0].sample_formats[0],
            fuchsia_hardware_audio::wire::DaiSampleFormat::kPcmSigned);
  EXPECT_EQ(formats[0].frame_formats.count(), 1);
  EXPECT_EQ(formats[0].frame_formats[0].frame_format_standard(),
            fuchsia_hardware_audio::wire::DaiFrameFormatStandard::kI2S);
  EXPECT_EQ(formats[0].frame_rates.count(), 1);
  EXPECT_EQ(formats[0].frame_rates[0], 48'000);
  EXPECT_EQ(formats[0].bits_per_slot.count(), 1);
  EXPECT_EQ(formats[0].bits_per_slot[0], 32);
  EXPECT_EQ(formats[0].bits_per_sample.count(), 1);
  EXPECT_EQ(formats[0].bits_per_sample[0], 24);
}

class TestStream2 : public IntelDspStream {
 public:
  TestStream2()
      : IntelDspStream(DspStream{.id = DspPipelineId{4},
                                 .host_format = kTestHostFormat,
                                 .dai_format = kTestDaiFormat,
                                 .is_i2s = false,
                                 .stream_id = 1,
                                 .is_input = true,
                                 .uid = AUDIO_STREAM_UNIQUE_ID_BUILTIN_HEADPHONE_JACK,
                                 .name = kTestProductName}) {}

  zx_status_t Bind() {
    fbl::AutoLock lock(obj_lock());
    return PublishDeviceLocked();
  }

 private:
  static constexpr AudioDataFormat kTestHostFormat = {
      .sampling_frequency = SamplingFrequency::FS_96000HZ,
      .bit_depth = BitDepth::DEPTH_32BIT,
      .channel_map = 0xFFFF3210,
      .channel_config = ChannelConfig::CONFIG_QUATRO,
      .interleaving_style = InterleavingStyle::PER_CHANNEL,
      .number_of_channels = 4,
      .valid_bit_depth = 24,
      .sample_type = SampleType::INT_MSB,
      .reserved = 0,
  };
  static constexpr AudioDataFormat kTestDaiFormat = {
      .sampling_frequency = SamplingFrequency::FS_96000HZ,
      .bit_depth = BitDepth::DEPTH_16BIT,
      .channel_map = 0xFFFF3210,
      .channel_config = ChannelConfig::CONFIG_QUATRO,
      .interleaving_style = InterleavingStyle::PER_CHANNEL,
      .number_of_channels = 4,
      .valid_bit_depth = 16,
      .sample_type = SampleType::INT_MSB,
      .reserved = 0,
  };
};

TEST(SstStream, GetDaiFormats2) {
  Binder tester;
  FakeController fake_controller(fake_ddk::kFakeParent);
  ASSERT_OK(fake_controller.Bind());
  fbl::RefPtr<TestCodec> codec = fbl::AdoptRef(new TestCodec);
  auto ret = codec->Bind(fake_controller.dev(), "test");
  ASSERT_OK(ret.status_value());
  auto stream = fbl::AdoptRef(new TestStream2);
  ASSERT_OK(codec->ActivateStream(stream));
  ASSERT_OK(stream->Bind());

  auto stream_client = GetDaiClient(tester.FidlClient<fuchsia_hardware_audio::DaiConnector>());
  ASSERT_TRUE(stream_client.is_valid());

  auto result = stream_client->GetDaiFormats();
  ASSERT_OK(result.status());
  auto& formats = result.value().value()->dai_formats;
  EXPECT_EQ(formats.count(), 1);
  EXPECT_EQ(formats[0].number_of_channels.count(), 1);
  EXPECT_EQ(formats[0].number_of_channels[0], 8);  // TDM.
  EXPECT_EQ(formats[0].sample_formats.count(), 1);
  EXPECT_EQ(formats[0].sample_formats[0],
            fuchsia_hardware_audio::wire::DaiSampleFormat::kPcmSigned);
  EXPECT_EQ(formats[0].frame_formats.count(), 1);
  EXPECT_EQ(formats[0].frame_formats[0].frame_format_standard(),
            fuchsia_hardware_audio::wire::DaiFrameFormatStandard::kTdm1);
  EXPECT_EQ(formats[0].frame_rates.count(), 1);
  EXPECT_EQ(formats[0].frame_rates[0], 96'000);
  EXPECT_EQ(formats[0].bits_per_slot.count(), 1);
  EXPECT_EQ(formats[0].bits_per_slot[0], 16);
  EXPECT_EQ(formats[0].bits_per_sample.count(), 1);
  EXPECT_EQ(formats[0].bits_per_sample[0], 16);

  codec->DeviceRelease();
  fake_controller.DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  fake_controller.DdkRelease();
}

}  // namespace audio::intel_hda
