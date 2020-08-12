// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/simple-codec/simple-codec-server.h>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zxtest/zxtest.h>

#include "../audio-stream.h"

namespace audio {
namespace astro {

namespace audio_fidl = ::llcpp::fuchsia::hardware::audio;

static constexpr uint32_t kTestFrameRate1 = 48000;
static constexpr uint32_t kTestFrameRate2 = 96000;

audio_fidl::PcmFormat GetDefaultPcmFormat() {
  audio_fidl::PcmFormat format;
  format.number_of_channels = 2;
  format.channels_to_use_bitmask = 0x03;
  format.sample_format = audio_fidl::SampleFormat::PCM_SIGNED;
  format.frame_rate = kTestFrameRate1;
  format.bytes_per_sample = 2;
  format.valid_bits_per_sample = 16;
  return format;
}

struct CodecTest;
using DeviceType = ddk::Device<CodecTest>;
struct CodecTest : public DeviceType, public SimpleCodecServer {
  explicit CodecTest(zx_device_t* device) : DeviceType(device), SimpleCodecServer(device) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }

  zx::status<DriverIds> Initialize() override { return zx::ok(DriverIds{}); }
  zx_status_t Shutdown() override { return ZX_OK; }
  zx_status_t Reset() override { return ZX_OK; }
  Info GetInfo() override { return {}; }
  zx_status_t Stop() override { return ZX_OK; }
  zx_status_t Start() override { return ZX_OK; }
  bool IsBridgeable() override { return true; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  std::vector<DaiSupportedFormats> GetDaiFormats() override { return {}; }
  zx_status_t SetDaiFormat(const DaiFormat& format) override {
    last_frame_rate_ = format.frame_rate;
    return ZX_OK;
  }
  GainFormat GetGainFormat() override { return {}; }
  GainState GetGainState() override { return {}; }
  void SetGainState(GainState state) override {}
  PlugState GetPlugState() override { return {}; }

  void DdkRelease() { delete this; }

  uint32_t last_frame_rate_ = {};
};

struct AmlTdmOutDeviceTest : public AmlTdmOutDevice {
  static std::unique_ptr<AmlTdmOutDeviceTest> Create(ddk_mock::MockMmioRegRegion& region) {
    return std::make_unique<AmlTdmOutDeviceTest>(region.GetMmioBuffer(), HIFI_PLL, TDM_OUT_C,
                                                 FRDDR_C, MCLK_C, 0,
                                                 metadata::AmlVersion::kS905D2G);
  }
  AmlTdmOutDeviceTest(ddk::MmioBuffer mmio, ee_audio_mclk_src_t clk_src, aml_tdm_out_t tdm,
                      aml_frddr_t ddr, aml_tdm_mclk_t mclk, uint32_t fifo_depth,
                      metadata::AmlVersion version)
      : AmlTdmOutDevice(std::move(mmio), clk_src, tdm, ddr, mclk, fifo_depth, version) {}
};

struct AstroI2sOutTest : public AstroTdmStream {
  AstroI2sOutTest(codec_protocol_t* codec_protocol, ddk_mock::MockMmioRegRegion& region)
      : AstroTdmStream(fake_ddk::kFakeParent, false) {
    codec_.SetProtocol(codec_protocol);
    metadata_.is_input = false;
    metadata_.number_of_channels = 2;
    metadata_.bus = metadata::AmlBus::TDM_C;
    metadata_.version = metadata::AmlVersion::kS905D2G;
    metadata_.tdm.type = metadata::TdmType::I2s;
    metadata_.tdm.codec = metadata::Codec::Tas27xx;
    aml_audio_ = AmlTdmOutDeviceTest::Create(region);
  }

  zx_status_t Init() __TA_REQUIRES(domain_token()) override {
    audio_stream_format_range_t range;
    range.min_channels = 2;
    range.max_channels = 2;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_frames_per_second = kTestFrameRate1;
    range.max_frames_per_second = kTestFrameRate2;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
    supported_formats_.push_back(range);

    fifo_depth_ = 16;

    cur_gain_state_ = {};

    SetInitialPlugState(AUDIO_PDNF_CAN_NOTIFY);

    snprintf(device_name_, sizeof(device_name_), "Testy Device");
    snprintf(mfr_name_, sizeof(mfr_name_), "Testy Inc");
    snprintf(prod_name_, sizeof(prod_name_), "Testy McTest");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

    InitHW();

    return ZX_OK;
  }
};

struct AstroPcmOutTest : public AstroI2sOutTest {
  AstroPcmOutTest(codec_protocol_t* codec_protocol, ddk_mock::MockMmioRegRegion& region)
      : AstroI2sOutTest(codec_protocol, region) {
    metadata_.tdm.type = metadata::TdmType::Pcm;
    metadata_.number_of_channels = 1;
  }
};

TEST(AstroTdm, InitializeI2sOut) {
  fake_ddk::Bind tester;

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM OUT for I2S.
  mock[0x580].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM OUT CTRL0 disable.
  // TDM OUT CTRL0 config, bitoffset 3, 2 slots, 16 bits per slot.
  mock[0x580].ExpectWrite(0x0001803f);
  // TDM OUT CTRL1 FRDDR C with 16 bits per sample.
  mock[0x584].ExpectWrite(0x02000F20);

  auto controller = audio::SimpleAudioStream::Create<AstroI2sOutTest>(&codec_proto, mock);
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  controller->DdkRelease();
}

TEST(AstroTdm, InitializePcmOut) {
  fake_ddk::Bind tester;

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM OUT for PCM.
  mock[0x580].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM OUT CTRL0 disable.
  // TDM OUT CTRL0 config, bitoffset 3, 1 slot, 32 bits per slot.
  mock[0x580].ExpectWrite(0x0001801f);
  // TDM OUT CTRL1 FRDDR C with 16 bits per sample.
  mock[0x584].ExpectWrite(0x02000F20);

  auto controller = audio::SimpleAudioStream::Create<AstroPcmOutTest>(&codec_proto, mock);
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  controller->DdkRelease();
}

TEST(AstroTdm, I2sOutChangeRate96K) {
  fake_ddk::Bind tester;

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // HW Initialize with 48kHz, set MCLK CTRL.
  mock[0x00c].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x00c].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x00c].ExpectRead(0x00000000).ExpectWrite(0x84000009);  // Enabled, HIFI PLL, set div to 9.

  // HW Initialize with 96kHz, set MCLK CTRL.
  mock[0x00c].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x00c].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x00c].ExpectRead(0x00000000).ExpectWrite(0x84000004);  // Enabled, HIFI PLL, set div to 4.

  auto controller = audio::SimpleAudioStream::Create<AstroI2sOutTest>(&codec_proto, mock);
  ASSERT_NOT_NULL(controller);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);
  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  // Default sets 48'000.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Changes to 96'000.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.frame_rate = kTestFrameRate2;  // Change it from the default at 48kHz.
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }

  // To make sure we have changed the rate in the codec make a sync call requiring codec reply
  // (we know the codec is single threaded, rate change is completed if received a reply).
  client.SetGain(audio_fidl::GainState{});

  // Check that we set the codec to the new rate.
  ASSERT_EQ(codec->last_frame_rate_, kTestFrameRate2);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  controller->DdkRelease();
}

struct AmlTdmInDeviceTest : public AmlTdmInDevice {
  static std::unique_ptr<AmlTdmInDeviceTest> Create(ddk_mock::MockMmioRegRegion& region) {
    return std::make_unique<AmlTdmInDeviceTest>(region.GetMmioBuffer(), HIFI_PLL, TDM_IN_C, TODDR_C,
                                                MCLK_C, 0, metadata::AmlVersion::kS905D2G);
  }
  AmlTdmInDeviceTest(ddk::MmioBuffer mmio, ee_audio_mclk_src_t clk_src, aml_tdm_in_t tdm,
                     aml_toddr_t ddr, aml_tdm_mclk_t mclk, uint32_t fifo_depth,
                     metadata::AmlVersion version)
      : AmlTdmInDevice(std::move(mmio), clk_src, tdm, ddr, mclk, fifo_depth, version) {}
};

struct AstroI2sInTest : public AstroTdmStream {
  AstroI2sInTest(ddk_mock::MockMmioRegRegion& region)
      : AstroTdmStream(fake_ddk::kFakeParent, true) {
    metadata_.is_input = true;
    metadata_.number_of_channels = 2;
    metadata_.bus = metadata::AmlBus::TDM_C;
    metadata_.version = metadata::AmlVersion::kS905D2G;
    metadata_.tdm.type = metadata::TdmType::I2s;
    metadata_.tdm.codec = metadata::Codec::None;
    aml_audio_ = AmlTdmInDeviceTest::Create(region);
  }

  zx_status_t Init() __TA_REQUIRES(domain_token()) override {
    audio_stream_format_range_t range;
    range.min_channels = 2;
    range.max_channels = 2;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_frames_per_second = kTestFrameRate1;
    range.max_frames_per_second = kTestFrameRate2;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
    supported_formats_.push_back(range);

    fifo_depth_ = 16;

    cur_gain_state_ = {};

    SetInitialPlugState(AUDIO_PDNF_CAN_NOTIFY);

    snprintf(device_name_, sizeof(device_name_), "Testy Device");
    snprintf(mfr_name_, sizeof(mfr_name_), "Testy Inc");
    snprintf(prod_name_, sizeof(prod_name_), "Testy McTest");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

    InitHW();

    return ZX_OK;
  }
};
struct AstroPcmInTest : public AstroI2sInTest {
  AstroPcmInTest(ddk_mock::MockMmioRegRegion& region) : AstroI2sInTest(region) {
    metadata_.number_of_channels = 1;
    metadata_.tdm.type = metadata::TdmType::Pcm;
  }
};

TEST(AstroTdm, InitializeI2sIn) {
  fake_ddk::Bind tester;

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM IN for I2S.
  mock[0x380].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM IN CTRL0 disable.
  // TDM IN CTRL config, I2S, source TDM IN C, bitoffset 4, 2 slots, 16 bits per slot.
  mock[0x380].ExpectWrite(0x0024001f);

  auto controller = audio::SimpleAudioStream::Create<AstroI2sInTest>(mock);
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  controller->DdkRelease();
}

TEST(AstroTdm, InitializePcmIn) {
  fake_ddk::Bind tester;

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM IN for PCM.
  mock[0x380].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM IN CTRL0 disable.
  // TDM IN CTRL config, TDM, source TDM IN C, bitoffset 4, 1 slot, 32 bits per slot.
  mock[0x380].ExpectWrite(0x0024001f);

  auto controller = audio::SimpleAudioStream::Create<AstroPcmInTest>(mock);
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  controller->DdkRelease();
}

}  // namespace astro
}  // namespace audio
