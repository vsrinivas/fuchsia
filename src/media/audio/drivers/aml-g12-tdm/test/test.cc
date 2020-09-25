// Copyright 2020 The Fuchsia Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/simple-codec/simple-codec-server.h>

#include <ddktl/protocol/composite.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zxtest/zxtest.h>

#include "../audio-stream.h"

namespace audio {
namespace aml_g12 {

namespace audio_fidl = ::llcpp::fuchsia::hardware::audio;

static constexpr uint32_t kTestFrameRate1 = 48000;
static constexpr uint32_t kTestFrameRate2 = 96000;
static constexpr size_t kMaxLanes = 4;
static constexpr float kTestGain = 1.234f;
static constexpr float kTestDeltaGain = 1.1f;

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
  zx_status_t Reset() override {
    started_ = true;
    return ZX_OK;
  }
  Info GetInfo() override { return {}; }
  zx_status_t Stop() override {
    started_ = false;
    return ZX_OK;
  }
  zx_status_t Start() override {
    started_ = true;
    return ZX_OK;
  }
  bool IsBridgeable() override { return true; }
  void SetBridgedMode(bool enable_bridged_mode) override {}
  std::vector<DaiSupportedFormats> GetDaiFormats() override { return {}; }
  zx_status_t SetDaiFormat(const DaiFormat& format) override {
    last_frame_rate_ = format.frame_rate;
    return ZX_OK;
  }
  GainFormat GetGainFormat() override {
    return {.min_gain_db = -10.f, .max_gain_db = 10.f, .can_mute = true, .can_agc = true};
  }
  GainState GetGainState() override { return {}; }
  void SetGainState(GainState state) override {
    muted_ = state.muted;
    gain_ = state.gain_db;
  }
  PlugState GetPlugState() override { return {}; }

  void DdkRelease() { delete this; }

  uint32_t last_frame_rate_ = {};
  bool started_ = false;
  bool muted_ = false;
  float gain_ = 0.f;
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

struct AmlG12I2sOutTest : public AmlG12TdmStream {
  void SetCommonDefaults() {
    metadata_.is_input = false;
    metadata_.mClockDivFactor = 10;
    metadata_.sClockDivFactor = 25;
    metadata_.number_of_channels = 2;
    metadata_.dai_number_of_channels = 2;
    metadata_.lanes_enable_mask[0] = 3;
    metadata_.bus = metadata::AmlBus::TDM_C;
    metadata_.version = metadata::AmlVersion::kS905D2G;
    metadata_.tdm.type = metadata::TdmType::I2s;
    metadata_.tdm.bits_per_sample = 16;
    metadata_.tdm.bits_per_slot = 32;
  }
  AmlG12I2sOutTest(codec_protocol_t* codec_protocol, ddk_mock::MockMmioRegRegion& region,
                   ddk::PDev pdev, ddk::GpioProtocolClient enable_gpio)
      : AmlG12TdmStream(fake_ddk::kFakeParent, false, std::move(pdev), std::move(enable_gpio)) {
    SetCommonDefaults();
    codecs_.push_back(SimpleCodecClient());
    codecs_[0].SetProtocol(codec_protocol);
    aml_audio_ = AmlTdmOutDeviceTest::Create(region);
    metadata_.tdm.number_of_codecs = 1;
    metadata_.tdm.codecs[0] = metadata::Codec::Tas27xx;
  }
  AmlG12I2sOutTest(codec_protocol_t* codec_protocol1, codec_protocol_t* codec_protocol2,
                   ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                   ddk::GpioProtocolClient enable_gpio)
      : AmlG12TdmStream(fake_ddk::kFakeParent, false, std::move(pdev), std::move(enable_gpio)) {
    SetCommonDefaults();
    codecs_.push_back(SimpleCodecClient());
    codecs_.push_back(SimpleCodecClient());
    codecs_[0].SetProtocol(codec_protocol1);
    codecs_[1].SetProtocol(codec_protocol2);
    aml_audio_ = AmlTdmOutDeviceTest::Create(region);
    metadata_.tdm.number_of_codecs = 2;
    metadata_.tdm.codecs[0] = metadata::Codec::Tas27xx;
    metadata_.tdm.codecs[1] = metadata::Codec::Tas27xx;
    metadata_.tdm.codecs_delta_gains[0] = kTestDeltaGain;
    metadata_.tdm.codecs_delta_gains[1] = 0.f;
  }

  zx_status_t Init() __TA_REQUIRES(domain_token()) override {
    audio_stream_format_range_t range;
    range.min_channels = 2;
    range.max_channels = 4;
    range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    range.min_frames_per_second = kTestFrameRate1;
    range.max_frames_per_second = kTestFrameRate2;
    range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
    supported_formats_.push_back(range);

    fifo_depth_ = 16;

    SetInitialPlugState(AUDIO_PDNF_CAN_NOTIFY);

    snprintf(device_name_, sizeof(device_name_), "Testy Device");
    snprintf(mfr_name_, sizeof(mfr_name_), "Testy Inc");
    snprintf(prod_name_, sizeof(prod_name_), "Testy McTest");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

    auto status = InitCodecsGain();
    if (status != ZX_OK) {
      return status;
    }

    return InitHW();
  }

  zx_status_t GetBuffer(const audio_proto::RingBufGetBufferReq& req, uint32_t* out_num_rb_frames,
                        zx::vmo* out_buffer) __TA_REQUIRES(domain_token()) override {
    zx::vmo rb;
    *out_num_rb_frames = req.min_ring_buffer_frames;
    zx::vmo::create(*out_num_rb_frames * 2 * 2, 0, &rb);
    constexpr uint32_t rights = ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER;
    return rb.duplicate(rights, out_buffer);
  }
};

TEST(AmlG12Tdm, InitializeI2sOut) {
  fake_ddk::Bind tester;

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM OUT for I2S.
  mock[0x580].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM OUT CTRL0 disable.
  // TDM OUT CTRL0 config, bitoffset 2, 2 slots, 32 bits per slot.
  mock[0x580].ExpectWrite(0x0001003f);
  // TDM OUT CTRL1 FRDDR C with 16 bits per sample.
  mock[0x584].ExpectWrite(0x02000F20);

  mock[0x050].ExpectWrite(0xc1807c3f);  // SCLK CTRL, enabled, 24 sdiv, 31 lrduty, 63 lrdiv.
  // SCLK CTRL1, clear delay, sclk_invert_ph0.
  mock[0x054].ExpectWrite(0x00000000).ExpectWrite(0x00000001);

  // CLK TDMOUT CTL, enable, no sclk_inv, sclk_ws_inv, mclk_ch 2.
  mock[0x098].ExpectWrite(0).ExpectWrite(0xd2200000);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

struct AmlG12PcmOutTest : public AmlG12I2sOutTest {
  AmlG12PcmOutTest(codec_protocol_t* codec_protocol, ddk_mock::MockMmioRegRegion& region,
                   ddk::PDev pdev, ddk::GpioProtocolClient enable_gpio)
      : AmlG12I2sOutTest(codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
    metadata_.number_of_channels = 1;
    metadata_.dai_number_of_channels = 1;
    metadata_.lanes_enable_mask[0] = 1;
    metadata_.tdm.type = metadata::TdmType::Tdm1;
    metadata_.tdm.bits_per_slot = 16;
    metadata_.tdm.number_of_codecs = 0;
    metadata_.tdm.sclk_on_raising = true;
  }
};

TEST(AmlG12Tdm, InitializePcmOut) {
  fake_ddk::Bind tester;

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM OUT for PCM.
  mock[0x580].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM OUT CTRL0 disable.
  // TDM OUT CTRL0 config, bitoffset 2, 1 slot, 16 bits per slot.
  mock[0x580].ExpectWrite(0x0001000f);
  // TDM OUT CTRL1 FRDDR C with 16 bits per sample.
  mock[0x584].ExpectWrite(0x02000F20);

  mock[0x050].ExpectWrite(0xc180000f);  // SCLK CTRL, enabled, 24 sdiv, 0 lrduty, 15 lrdiv.
  // SCLK CTRL1, clear delay, no sclk_invert_ph0.
  mock[0x054].ExpectWrite(0x00000000).ExpectWrite(0x00000000);

  // CLK TDMOUT CTL, enable, no sclk_inv, sclk_ws_inv, mclk_ch 2.
  mock[0x098].ExpectWrite(0).ExpectWrite(0xd2200000);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12PcmOutTest>(
      &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

struct AmlG12LjtOutTest : public AmlG12I2sOutTest {
  AmlG12LjtOutTest(codec_protocol_t* codec_protocol, ddk_mock::MockMmioRegRegion& region,
                   ddk::PDev pdev, ddk::GpioProtocolClient enable_gpio)
      : AmlG12I2sOutTest(codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
    metadata_.number_of_channels = 2;
    metadata_.lanes_enable_mask[0] = 3;
    metadata_.tdm.type = metadata::TdmType::StereoLeftJustified;
    metadata_.tdm.bits_per_sample = 16;
    metadata_.tdm.bits_per_slot = 16;
  }
};

TEST(AmlG12Tdm, InitializeLeftJustifiedOut) {
  fake_ddk::Bind tester;

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM OUT for LeftJustified.
  mock[0x580].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM OUT CTRL0 disable.
  // TDM OUT CTRL0 config, bitoffset 3, 2 slots, 16 bits per slot.
  mock[0x580].ExpectWrite(0x0001802f);
  // TDM OUT CTRL1 FRDDR C with 16 bits per sample.
  mock[0x584].ExpectWrite(0x02000F20);

  mock[0x050].ExpectWrite(0xc1803c1f);  // SCLK CTRL, enabled, 24 sdiv, 15 lrduty, 31 lrdiv.
  // SCLK CTRL1, clear delay, sclk_invert_ph0.
  mock[0x054].ExpectWrite(0x00000000).ExpectWrite(0x00000001);

  // CLK TDMOUT CTL, enable, no sclk_inv, sclk_ws_inv, mclk_ch 2.
  mock[0x098].ExpectWrite(0).ExpectWrite(0xd2200000);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12LjtOutTest>(
      &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

struct AmlG12Tdm1OutTest : public AmlG12I2sOutTest {
  AmlG12Tdm1OutTest(codec_protocol_t* codec_protocol, ddk_mock::MockMmioRegRegion& region,
                    ddk::PDev pdev, ddk::GpioProtocolClient enable_gpio)
      : AmlG12I2sOutTest(codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
    metadata_.number_of_channels = 4;
    metadata_.dai_number_of_channels = 4;
    metadata_.lanes_enable_mask[0] = 0xf;
    metadata_.tdm.type = metadata::TdmType::Tdm1;
    metadata_.tdm.bits_per_slot = 16;
  }
};

TEST(AmlG12Tdm, InitializeTdm1Out) {
  fake_ddk::Bind tester;

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM OUT for Tdm1.
  mock[0x580].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM OUT CTRL0 disable.
  // TDM OUT CTRL0 config, bitoffset 3, 4 slots, 16 bits per slot.
  mock[0x580].ExpectWrite(0x0001806f);
  // TDM OUT CTRL1 FRDDR C with 16 bits per sample.
  mock[0x584].ExpectWrite(0x02000F20);

  mock[0x050].ExpectWrite(0xc180003f);  // SCLK CTRL, enabled, 24 sdiv, 0 lrduty, 63 lrdiv.
  // SCLK CTRL1, clear delay, sclk_invert_ph0.
  mock[0x054].ExpectWrite(0x00000000).ExpectWrite(0x00000001);

  // CLK TDMOUT CTL, enable, no sclk_inv, sclk_ws_inv, mclk_ch 2.
  mock[0x098].ExpectWrite(0).ExpectWrite(0xd2200000);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12Tdm1OutTest>(
      &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

TEST(AmlG12Tdm, I2sOutCodecsStartedAndMuted) {
  fake_ddk::Bind tester;

  auto codec1 = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec2 = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec1_proto = codec1->GetProto();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      &codec1_proto, &codec2_proto, unused_mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);
  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

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

  // Check we started (al least not stopped) both codecs and set them to muted.
  ASSERT_TRUE(codec1->started_);
  ASSERT_TRUE(codec2->started_);
  ASSERT_TRUE(codec1->muted_);
  ASSERT_TRUE(codec2->muted_);

  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

TEST(AmlG12Tdm, I2sOutSetGainState) {
  fake_ddk::Bind tester;

  auto codec1 = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec2 = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec1_proto = codec1->GetProto();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      &codec1_proto, &codec2_proto, unused_mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);
  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  {
    auto builder = audio_fidl::GainState::UnownedBuilder();
    fidl::aligned<bool> mute = true;
    fidl::aligned<bool> agc = false;
    fidl::aligned<float> gain = kTestGain;
    builder.set_muted(fidl::unowned_ptr(&mute));
    builder.set_agc_enabled(fidl::unowned_ptr(&agc));
    builder.set_gain_db(fidl::unowned_ptr(&gain));
    client.SetGain(builder.build());

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    // In this test we want to get the gain state anyways.
    auto gain_state =
        audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(client.channel()));
    ASSERT_TRUE(gain_state->gain_state.has_agc_enabled());
    ASSERT_FALSE(gain_state->gain_state.agc_enabled());
    ASSERT_EQ(gain_state->gain_state.gain_db(), kTestGain);

    ASSERT_EQ(codec1->gain_, kTestGain + kTestDeltaGain);
    ASSERT_EQ(codec2->gain_, kTestGain);
    ASSERT_TRUE(codec1->muted_);
    ASSERT_TRUE(codec2->muted_);
  }

  {
    auto builder = audio_fidl::GainState::UnownedBuilder();
    fidl::aligned<bool> mute = false;
    fidl::aligned<bool> agc = true;
    fidl::aligned<float> gain = kTestGain;
    builder.set_muted(fidl::unowned_ptr(&mute));
    builder.set_agc_enabled(fidl::unowned_ptr(&agc));
    builder.set_gain_db(fidl::unowned_ptr(&gain));
    client.SetGain(builder.build());

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    // In this test we want to get the gain state anyways.
    auto gain_state =
        audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(client.channel()));

    ASSERT_TRUE(gain_state->gain_state.has_agc_enabled());
    ASSERT_TRUE(gain_state->gain_state.agc_enabled());
    ASSERT_EQ(gain_state->gain_state.gain_db(), kTestGain);

    ASSERT_EQ(codec1->gain_, kTestGain + kTestDeltaGain);
    ASSERT_EQ(codec2->gain_, kTestGain);
    ASSERT_TRUE(codec1->muted_);  // override_mute_ forces muted in the codec.
    ASSERT_TRUE(codec2->muted_);  // override_mute_ forces muted in the codec.
  }

  {
    // Now we start the ring buffer so override_mute_ gets cleared.
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    auto vmo = audio_fidl::RingBuffer::Call::GetVmo(zx::unowned_channel(local), 8192, 0);
    ASSERT_OK(vmo.status());
    auto start = audio_fidl::RingBuffer::Call::Start(zx::unowned_channel(local));
    ASSERT_OK(start.status());

    // Now we set gain again.
    auto builder2 = audio_fidl::GainState::UnownedBuilder();
    fidl::aligned<bool> mute = false;
    fidl::aligned<bool> agc = false;  // Change agc from last one, so the Watch below replies.
    fidl::aligned<float> gain = kTestGain;
    builder2.set_muted(fidl::unowned_ptr(&mute));
    builder2.set_agc_enabled(fidl::unowned_ptr(&agc));
    builder2.set_gain_db(fidl::unowned_ptr(&gain));
    client.SetGain(builder2.build());

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    // In this test we want to get the gain state anyways.
    auto gain_state =
        audio_fidl::StreamConfig::Call::WatchGainState(zx::unowned_channel(client.channel()));

    ASSERT_TRUE(gain_state->gain_state.has_agc_enabled());
    ASSERT_FALSE(gain_state->gain_state.agc_enabled());
    ASSERT_EQ(gain_state->gain_state.gain_db(), kTestGain);

    ASSERT_EQ(codec1->gain_, kTestGain + kTestDeltaGain);
    ASSERT_EQ(codec2->gain_, kTestGain);
    ASSERT_FALSE(codec1->muted_);  // override_mute_ is cleared, we were able to set mute to false.
    ASSERT_FALSE(codec2->muted_);  // override_mute_ is cleared, we were able to set mute to false.
  }

  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

TEST(AmlG12Tdm, I2sOutOneCodecCantAgc) {
  struct CodecCantAgcTest : public CodecTest {
    explicit CodecCantAgcTest(zx_device_t* device) : CodecTest(device) {}
    GainFormat GetGainFormat() override {
      return {.min_gain_db = -10.f, .max_gain_db = 10.f, .can_mute = true, .can_agc = false};
    }
  };

  fake_ddk::Bind tester;

  auto codec1 = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec2 = SimpleCodecServer::Create<CodecCantAgcTest>(fake_ddk::kFakeParent);
  auto codec1_proto = codec1->GetProto();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      &codec1_proto, &codec2_proto, unused_mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);
  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  auto props = audio_fidl::StreamConfig::Call::GetProperties(zx::unowned_channel(client.channel()));
  ASSERT_OK(props.status());

  EXPECT_TRUE(props->properties.can_mute());
  EXPECT_FALSE(props->properties.can_agc());

  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

TEST(AmlG12Tdm, I2sOutOneCodecCantMute) {
  struct CodecCantMuteTest : public CodecTest {
    explicit CodecCantMuteTest(zx_device_t* device) : CodecTest(device) {}
    GainFormat GetGainFormat() override {
      return {.min_gain_db = -10.f, .max_gain_db = 10.f, .can_mute = false, .can_agc = true};
    }
  };

  fake_ddk::Bind tester;

  auto codec1 = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec2 = SimpleCodecServer::Create<CodecCantMuteTest>(fake_ddk::kFakeParent);
  auto codec1_proto = codec1->GetProto();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      &codec1_proto, &codec2_proto, unused_mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);
  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  auto props = audio_fidl::StreamConfig::Call::GetProperties(zx::unowned_channel(client.channel()));
  ASSERT_OK(props.status());

  EXPECT_FALSE(props->properties.can_mute());
  EXPECT_TRUE(props->properties.can_agc());

  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

TEST(AmlG12Tdm, I2sOutChangeRate96K) {
  fake_ddk::Bind tester;

  auto codec1 = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec2 = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec1_proto = codec1->GetProto();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // HW Initialize with 48kHz, set MCLK CTRL.
  mock[0x00c].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x00c].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x00c].ExpectRead(0x00000000).ExpectWrite(0x84000009);  // Enabled, HIFI PLL, set div to 9.

  // HW Initialize with requested 48kHz, set MCLK CTRL.
  mock[0x00c].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x00c].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x00c].ExpectRead(0x00000000).ExpectWrite(0x84000009);  // Enabled, HIFI PLL, set div to 9.

  // HW Initialize with requested 96kHz, set MCLK CTRL.
  mock[0x00c].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x00c].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x00c].ExpectRead(0x00000000).ExpectWrite(0x84000004);  // Enabled, HIFI PLL, set div to 4.

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      &codec1_proto, &codec2_proto, mock, unused_pdev, enable_gpio.GetProto());
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
  ASSERT_EQ(codec1->last_frame_rate_, kTestFrameRate2);
  ASSERT_EQ(codec2->last_frame_rate_, kTestFrameRate2);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

struct AmlTdmOutDeviceMuteTest : public AmlTdmOutDevice {
  AmlTdmOutDeviceMuteTest(ddk_mock::MockMmioRegRegion& region)
      : AmlTdmOutDevice(region.GetMmioBuffer(), HIFI_PLL, TDM_OUT_C, FRDDR_C, MCLK_C, 0,
                        metadata::AmlVersion::kS905D2G) {}
  zx_status_t ConfigTdmLane(size_t lane, uint32_t enable_mask, uint32_t mute_mask) override {
    if (lane >= kMaxLanes) {
      return ZX_ERR_INTERNAL;
    }
    last_enable_mask_[lane] = enable_mask;
    last_mute_mask_[lane] = mute_mask;
    return ZX_OK;
  }
  uint32_t last_enable_mask_[kMaxLanes] = {};
  uint32_t last_mute_mask_[kMaxLanes] = {};
};

TEST(AmlG12Tdm, EnableAndMuteChannelsPcm1Channel) {
  fake_ddk::Bind tester;

  struct AmlG12PcmOutMuteTest : public AmlG12I2sOutTest {
    AmlG12PcmOutMuteTest(codec_protocol_t* codec_protocol, ddk_mock::MockMmioRegRegion& region,
                         ddk::PDev pdev, ddk::GpioProtocolClient enable_gpio)
        : AmlG12I2sOutTest(codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
      metadata_.number_of_channels = 1;
      metadata_.dai_number_of_channels = 1;
      metadata_.lanes_enable_mask[0] = 1;  // 1 channel.
      metadata_.tdm.type = metadata::TdmType::Tdm1;
      metadata_.tdm.bits_per_slot = 16;
      metadata_.tdm.sclk_on_raising = true;
      aml_audio_ = std::make_unique<AmlTdmOutDeviceMuteTest>(region);
    }
    AmlTdmDevice* GetAmlTdmDevice() { return aml_audio_.get(); }
  };

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12PcmOutMuteTest>(
      &codec_proto, unused_mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  auto aml = static_cast<AmlTdmOutDeviceMuteTest*>(controller->GetAmlTdmDevice());

  ZX_ASSERT(metadata::kMaxNumberOfLanes >= 2);  // Test assumes at least 2.

  // 1 channels enabled 1 lane.

  // Initially 1 channel enabled nothing muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 1);
  EXPECT_EQ(aml->last_enable_mask_[1], 0);
  EXPECT_EQ(aml->last_mute_mask_[0], 0);
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  // 1st case configure and keep everything enabled.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.number_of_channels = 4;
    pcm_format.channels_to_use_bitmask = 0xf;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Still one channel enabled, nothing muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 1);
  EXPECT_EQ(aml->last_enable_mask_[1], 0);
  EXPECT_EQ(aml->last_mute_mask_[0], 0);
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  // 2nd case, disable the channel.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    // TODO(andresoportus): Make AUDIO_SET_FORMAT_REQ_BITMASK_DISABLED != 0, so bitmask could be 0.
    pcm_format.channels_to_use_bitmask = 0xe;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Now the channel gets muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 1);
  EXPECT_EQ(aml->last_enable_mask_[1], 0);
  EXPECT_EQ(aml->last_mute_mask_[0], 1);  // Mutes 1 channel in lane 0.
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

TEST(AmlG12Tdm, EnableAndMuteChannelsTdm2Lanes) {
  fake_ddk::Bind tester;

  struct AmlG12Tdm2LanesOutMuteTest : public AmlG12I2sOutTest {
    AmlG12Tdm2LanesOutMuteTest(codec_protocol_t* codec_protocol,
                               ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                               ddk::GpioProtocolClient enable_gpio)
        : AmlG12I2sOutTest(codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
      metadata_.number_of_channels = 4;
      metadata_.lanes_enable_mask[0] = 3;  // L + R tweeters.
      metadata_.lanes_enable_mask[1] = 3;  // Woofer in lane 1.
      metadata_.tdm.type = metadata::TdmType::Tdm1;
      metadata_.tdm.bits_per_slot = 16;
      aml_audio_ = std::make_unique<AmlTdmOutDeviceMuteTest>(region);
    }
    AmlTdmDevice* GetAmlTdmDevice() { return aml_audio_.get(); }
  };

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12Tdm2LanesOutMuteTest>(
      &codec_proto, unused_mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  auto aml = static_cast<AmlTdmOutDeviceMuteTest*>(controller->GetAmlTdmDevice());

  ZX_ASSERT(metadata::kMaxNumberOfLanes >= 2);  // Test assumes at least 2.

  // 4 channels enabled 2 lanes.

  // Initially 4 channels enabled nothing muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 3);
  EXPECT_EQ(aml->last_enable_mask_[1], 3);
  EXPECT_EQ(aml->last_mute_mask_[0], 0);
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  // 1st case configure and keep everything enabled.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.number_of_channels = 4;
    pcm_format.channels_to_use_bitmask = 0xf;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Still all enabled, nothing muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 3);
  EXPECT_EQ(aml->last_enable_mask_[1], 3);
  EXPECT_EQ(aml->last_mute_mask_[0], 0);
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  // 2nd case configure and enable only one channel.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = 1;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Now 3 channels get muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 3);
  EXPECT_EQ(aml->last_enable_mask_[1], 3);
  EXPECT_EQ(aml->last_mute_mask_[0], 2);  // Mutes 1 channel in lane 0.
  EXPECT_EQ(aml->last_mute_mask_[1], 3);  // Mutes 2 channels in lane 1.

  // 3rd case configure and enable 2 channels.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = 0xa;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Now 2 channels get muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 3);
  EXPECT_EQ(aml->last_enable_mask_[1], 3);
  EXPECT_EQ(aml->last_mute_mask_[0], 1);  // Mutes 1 channel in lane 0.
  EXPECT_EQ(aml->last_mute_mask_[1], 1);  // Mutes 1 channel in lane 1.

  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

TEST(AmlG12Tdm, EnableAndMuteChannelsTdm1Lane) {
  fake_ddk::Bind tester;

  struct AmlG12Tdm1LaneOutMuteTest : public AmlG12I2sOutTest {
    AmlG12Tdm1LaneOutMuteTest(codec_protocol_t* codec_protocol, ddk_mock::MockMmioRegRegion& region,
                              ddk::PDev pdev, ddk::GpioProtocolClient enable_gpio)
        : AmlG12I2sOutTest(codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
      metadata_.number_of_channels = 4;
      metadata_.dai_number_of_channels = 4;
      metadata_.lanes_enable_mask[0] = 0xf;  // All 4 speakers in lane 0.
      metadata_.tdm.type = metadata::TdmType::Tdm1;
      metadata_.tdm.bits_per_slot = 16;
      aml_audio_ = std::make_unique<AmlTdmOutDeviceMuteTest>(region);
    }
    AmlTdmDevice* GetAmlTdmDevice() { return aml_audio_.get(); }
  };

  auto codec = SimpleCodecServer::Create<CodecTest>(fake_ddk::kFakeParent);
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12Tdm1LaneOutMuteTest>(
      &codec_proto, unused_mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  audio_fidl::Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  auto aml = static_cast<AmlTdmOutDeviceMuteTest*>(controller->GetAmlTdmDevice());

  ZX_ASSERT(metadata::kMaxNumberOfLanes >= 2);  // Test assumes at least 2.

  // 4 channels enabled 1 lane.

  // Initially 4 channels enabled nothing muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 0xf);
  EXPECT_EQ(aml->last_enable_mask_[1], 0);
  EXPECT_EQ(aml->last_mute_mask_[0], 0);
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  // 1st case configure and keep everything enabled.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.number_of_channels = 4;
    pcm_format.channels_to_use_bitmask = 0xf;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Still all enabled, nothing muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 0xf);
  EXPECT_EQ(aml->last_enable_mask_[1], 0);
  EXPECT_EQ(aml->last_mute_mask_[0], 0);
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  // 2nd case configure and enable only one channel.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = 1;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Now 3 channels get muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 0xf);
  EXPECT_EQ(aml->last_enable_mask_[1], 0);
  EXPECT_EQ(aml->last_mute_mask_[0], 0xe);  // Mutes 3 channels in lane 0.
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  // 3rd case configure and enable 2 channels.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = 0xa;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // Now 2 channels get muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 0xf);
  EXPECT_EQ(aml->last_enable_mask_[1], 0);
  EXPECT_EQ(aml->last_mute_mask_[0], 0x5);  // Mutes 2 channels in lane 0.
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
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

struct AmlG12I2sInTest : public AmlG12TdmStream {
  AmlG12I2sInTest(ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                  ddk::GpioProtocolClient enable_gpio)
      : AmlG12TdmStream(fake_ddk::kFakeParent, true, std::move(pdev), std::move(enable_gpio)) {
    metadata_.is_input = true;
    metadata_.mClockDivFactor = 10;
    metadata_.sClockDivFactor = 25;
    metadata_.number_of_channels = 2;
    metadata_.dai_number_of_channels = 2;
    metadata_.lanes_enable_mask[0] = 3;
    metadata_.bus = metadata::AmlBus::TDM_C;
    metadata_.version = metadata::AmlVersion::kS905D2G;
    metadata_.tdm.type = metadata::TdmType::I2s;
    metadata_.tdm.bits_per_sample = 16;
    metadata_.tdm.bits_per_slot = 32;
    metadata_.tdm.number_of_codecs = 0;
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

struct AmlG12PcmInTest : public AmlG12I2sInTest {
  AmlG12PcmInTest(ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                  ddk::GpioProtocolClient enable_gpio)
      : AmlG12I2sInTest(region, std::move(pdev), std::move(enable_gpio)) {
    metadata_.number_of_channels = 1;
    metadata_.dai_number_of_channels = 1;
    metadata_.lanes_enable_mask[0] = 1;
    metadata_.tdm.type = metadata::TdmType::Tdm1;
    metadata_.tdm.bits_per_slot = 16;
    metadata_.tdm.sclk_on_raising = true;
  }
};

TEST(AmlG12Tdm, InitializeI2sIn) {
  fake_ddk::Bind tester;

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM IN for I2S.
  mock[0x380].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM IN CTRL0 disable.
  // TDM IN CTRL config, I2S, source TDM IN C, I2S mode, bitoffset 3, 2 slots, 16 bits per slot.
  mock[0x380].ExpectWrite(0x4023001f);

  mock[0x050].ExpectWrite(0xc1807c3f);  // SCLK CTRL, enabled, 24 sdiv, 31 lrduty, 63 lrdiv.
  // SCLK CTRL1, clear delay, sclk_invert_ph0.
  mock[0x054].ExpectWrite(0x00000000).ExpectWrite(0x00000001);

  // CLK TDMIN CTL, enable, sclk_inv, no sclk_ws_inv, mclk_ch 2.
  mock[0x088].ExpectWrite(0).ExpectWrite(0xe2200000);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller =
      audio::SimpleAudioStream::Create<AmlG12I2sInTest>(mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

TEST(AmlG12Tdm, InitializePcmIn) {
  fake_ddk::Bind tester;

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM IN for PCM.
  mock[0x380].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM IN CTRL0 disable.
  // TDM IN CTRL config, TDM, source TDM IN C, TDM mode, bitoffset 3, 1 slot, 16 bits per slot.
  mock[0x380].ExpectWrite(0x0023000f);

  mock[0x050].ExpectWrite(0xc180000f);  // SCLK CTRL, enabled, 24 sdiv, 0 lrduty, 15 lrdiv.
  // SCLK CTRL1, clear delay, no sclk_invert_ph0.
  mock[0x054].ExpectWrite(0x00000000).ExpectWrite(0x00000000);

  // CLK TDMIN CTL, enable, sclk_inv, no sclk_ws_inv, mclk_ch 2.
  mock[0x088].ExpectWrite(0).ExpectWrite(0xe2200000);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller =
      audio::SimpleAudioStream::Create<AmlG12PcmInTest>(mock, unused_pdev, enable_gpio.GetProto());
  ASSERT_NOT_NULL(controller);

  mock.VerifyAll();
  controller->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  controller->DdkRelease();
}

}  // namespace aml_g12
}  // namespace audio
