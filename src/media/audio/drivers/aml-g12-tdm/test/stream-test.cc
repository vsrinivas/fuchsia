// Copyright 2020 The Fuchsia Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.hardware.audio/cpp/wire.h>
#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/ddk/metadata.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <lib/sync/completion.h>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zxtest/zxtest.h>

#include "../audio-stream.h"
#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
namespace audio::aml_g12 {

namespace audio_fidl = fuchsia_hardware_audio;

static constexpr float kTestGain = 2.f;
static constexpr float kTestDeltaGain = 1.f;
static constexpr float kTestTurnOnNsecs = 12345;
static constexpr float kTestTurnOffNsecs = 67890;

audio_fidl::wire::PcmFormat GetDefaultPcmFormat() {
  audio_fidl::wire::PcmFormat format;
  format.number_of_channels = 2;
  format.sample_format = audio_fidl::wire::SampleFormat::kPcmSigned;
  format.frame_rate = 48'000;
  format.bytes_per_sample = 2;
  format.valid_bits_per_sample = 16;
  return format;
}

fidl::WireSyncClient<audio_fidl::StreamConfig> GetStreamClient(
    fidl::ClientEnd<audio_fidl::StreamConfigConnector> client) {
  fidl::WireSyncClient client_wrap{std::move(client)};
  if (!client_wrap.is_valid()) {
    return {};
  }
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfig>();
  if (!endpoints.is_ok()) {
    return {};
  }
  auto [stream_channel_local, stream_channel_remote] = *std::move(endpoints);
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)client_wrap->Connect(std::move(stream_channel_remote));
  return fidl::WireSyncClient<audio_fidl::StreamConfig>(std::move(stream_channel_local));
}

class CodecTest : public SimpleCodecServer {
 public:
  explicit CodecTest(zx_device_t* device) : SimpleCodecServer(device) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }

  zx::result<DriverIds> Initialize() override { return zx::ok(DriverIds{}); }
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
  DaiSupportedFormats GetDaiFormats() override {
    DaiSupportedFormats formats;
    formats.number_of_channels.push_back(2);
    formats.sample_formats.push_back(SampleFormat::PCM_SIGNED);
    formats.frame_formats.push_back(FrameFormat::I2S);
    formats.frame_rates.push_back(48'000);
    formats.bits_per_slot.push_back(16);
    formats.bits_per_sample.push_back(16);
    return formats;
  }
  zx::result<CodecFormatInfo> SetDaiFormat(const DaiFormat& format) override {
    last_frame_rate_ = format.frame_rate;
    CodecFormatInfo format_info = {};
    format_info.set_turn_on_delay(kTestTurnOnNsecs);
    format_info.set_turn_off_delay(kTestTurnOffNsecs);
    return zx::ok(std::move(format_info));
  }
  GainFormat GetGainFormat() override {
    return {
        .min_gain = -10.f, .max_gain = 10.f, .gain_step = .5f, .can_mute = true, .can_agc = true};
  }
  GainState GetGainState() override { return {}; }
  void SetGainState(GainState state) override {
    muted_ = state.muted;
    gain_ = state.gain;
    sync_completion_signal(&set_gain_completion_);
  }
  void DdkRelease() { delete this; }

  void wait_for_set_gain_completion() {
    sync_completion_wait(&set_gain_completion_, ZX_TIME_INFINITE);
    sync_completion_reset(&set_gain_completion_);
  }
  uint32_t last_frame_rate() { return last_frame_rate_; }
  bool started() { return started_; }
  bool muted() { return muted_; }
  float gain() { return gain_; }

 private:
  uint32_t last_frame_rate_ = 0;
  bool started_ = false;
  bool muted_ = false;
  float gain_ = 0.f;
  sync_completion_t set_gain_completion_;
};

struct AmlG12I2sOutTest : public AmlG12TdmStream {
  void SetCommonDefaults() {
    metadata_.is_input = false;
    metadata_.mClockDivFactor = 10;
    metadata_.sClockDivFactor = 25;
    metadata_.bus = metadata::AmlBus::TDM_C;
    metadata_.version = metadata::AmlVersion::kS905D2G;
    metadata_.dai.type = metadata::DaiType::I2s;
    metadata_.dai.number_of_channels = 2;
    metadata_.dai.bits_per_sample = 16;
    metadata_.dai.bits_per_slot = 32;
  }
  AmlG12I2sOutTest(zx_device_t* parent, codec_protocol_t* codec_protocol,
                   ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                   ddk::GpioProtocolClient enable_gpio)
      : AmlG12TdmStream(parent, false, std::move(pdev), std::move(enable_gpio)) {
    SetCommonDefaults();
    codecs_.push_back(SimpleCodecClient());
    codecs_[0].SetProtocol(codec_protocol);
    aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, region.GetMmioBuffer());
    metadata_.ring_buffer.number_of_channels = 2;
    metadata_.lanes_enable_mask[0] = 3;
    metadata_.codecs.number_of_codecs = 1;
    metadata_.codecs.types[0] = metadata::CodecType::Tas27xx;
    metadata_.codecs.ring_buffer_channels_to_use_bitmask[0] = 1;
  }
  AmlG12I2sOutTest(zx_device_t* parent, const std::vector<codec_protocol_t*>& codec_protocols,
                   ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                   ddk::GpioProtocolClient enable_gpio)
      : AmlG12TdmStream(parent, false, std::move(pdev), std::move(enable_gpio)) {
    SetCommonDefaults();
    aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, region.GetMmioBuffer());
    // Simply one ring buffer channel per codec.
    metadata_.ring_buffer.number_of_channels = codec_protocols.size();
    metadata_.codecs.number_of_codecs = codec_protocols.size();
    for (size_t i = 0; i < codec_protocols.size(); ++i) {
      codecs_.push_back(SimpleCodecClient());
      codecs_[i].SetProtocol(codec_protocols[i]);
      metadata_.lanes_enable_mask[i] = (1 << i);  // Simply one lane per codec.
      metadata_.codecs.types[i] = metadata::CodecType::Tas27xx;
      metadata_.codecs.delta_gains[i] = 0.f;
      metadata_.codecs.ring_buffer_channels_to_use_bitmask[i] = (1 << i);
    }
    metadata_.codecs.delta_gains[0] = kTestDeltaGain;  // Only first one non-zero.
  }

  zx_status_t Init() __TA_REQUIRES(domain_token()) override {
    SimpleAudioStream::SupportedFormat format = {};
    format.range.min_channels = 2;
    format.range.max_channels = 4;
    format.range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    format.range.min_frames_per_second = 8'000;
    format.range.max_frames_per_second = 96'000;
    format.range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
    supported_formats_.push_back(std::move(format));

    fifo_depth_ = 16;

    SetInitialPlugState(AUDIO_PDNF_CAN_NOTIFY);

    snprintf(device_name_, sizeof(device_name_), "Testy Device");
    snprintf(mfr_name_, sizeof(mfr_name_), "Testy Inc");
    snprintf(prod_name_, sizeof(prod_name_), "Testy McTest");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

    InitDaiFormats();
    auto status = InitCodecsGain();
    if (status != ZX_OK) {
      return status;
    }
    constexpr uint64_t channels_to_use = 0x03;
    return aml_audio_->InitHW(metadata_, channels_to_use, 48'000);
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
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<CodecTest>();
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
      fake_parent.get(), &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  mock.VerifyAll();
  enable_gpio.VerifyAndClear();
}

struct AmlG12PcmOutTest : public AmlG12I2sOutTest {
  AmlG12PcmOutTest(zx_device_t* parent, codec_protocol_t* codec_protocol,
                   ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                   ddk::GpioProtocolClient enable_gpio)
      : AmlG12I2sOutTest(parent, codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
    metadata_.bus = metadata::AmlBus::TDM_A;
    metadata_.ring_buffer.number_of_channels = 1;
    metadata_.lanes_enable_mask[0] = 1;
    metadata_.dai.type = metadata::DaiType::Tdm1;
    metadata_.dai.number_of_channels = 1;
    metadata_.dai.bits_per_slot = 16;
    metadata_.codecs.number_of_codecs = 0;
    metadata_.dai.sclk_on_raising = true;
    aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, region.GetMmioBuffer());
  }
};

TEST(AmlG12Tdm, InitializePcmOut) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<CodecTest>();
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // Configure TDM OUT A for PCM. EE_AUDIO_TDMOUT_A_CTRL0.
  mock[0x500].ExpectRead(0xffffffff).ExpectWrite(0x7fffffff);  // TDM OUT CTRL0 disable.
  // TDM OUT A CTRL0 config, bitoffset 2, 1 slot, 16 bits per slot.
  mock[0x500].ExpectWrite(0x0001000f);
  // TDM OUT A CTRL1 FRDDR A with 16 bits per sample.
  mock[0x504].ExpectWrite(0x00000F20);

  // SCLK A CTRL, enabled, 24 sdiv, 0 lrduty, 15 lrdiv. EE_AUDIO_MST_A_SCLK_CTRL0.
  mock[0x040].ExpectWrite(0xc180000f);
  // SCLK A CTRL1, clear delay, no sclk_invert_ph0. EE_AUDIO_MST_A_SCLK_CTRL1.
  mock[0x044].ExpectWrite(0x00000000).ExpectWrite(0x00000000);

  // CLK TDMOUT A CTL, enable, no sclk_inv, sclk_ws_inv, mclk_ch 0. EE_AUDIO_CLK_TDMOUT_A_CTRL.
  mock[0x090].ExpectWrite(0).ExpectWrite(0xd0000000);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  auto controller = audio::SimpleAudioStream::Create<AmlG12PcmOutTest>(
      fake_parent.get(), &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  mock.VerifyAll();
  enable_gpio.VerifyAndClear();
}

struct AmlG12LjtOutTest : public AmlG12I2sOutTest {
  AmlG12LjtOutTest(zx_device_t* parent, codec_protocol_t* codec_protocol,
                   ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                   ddk::GpioProtocolClient enable_gpio)
      : AmlG12I2sOutTest(parent, codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
    metadata_.ring_buffer.number_of_channels = 2;
    metadata_.lanes_enable_mask[0] = 3;
    metadata_.dai.type = metadata::DaiType::StereoLeftJustified;
    metadata_.dai.bits_per_sample = 16;
    metadata_.dai.bits_per_slot = 16;
    aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, region.GetMmioBuffer());
  }
};

TEST(AmlG12Tdm, InitializeLeftJustifiedOut) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<CodecTest>();
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
      fake_parent.get(), &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  mock.VerifyAll();
  enable_gpio.VerifyAndClear();
}

struct AmlG12Tdm1OutTest : public AmlG12I2sOutTest {
  AmlG12Tdm1OutTest(zx_device_t* parent, codec_protocol_t* codec_protocol,
                    ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                    ddk::GpioProtocolClient enable_gpio)
      : AmlG12I2sOutTest(parent, codec_protocol, region, std::move(pdev), std::move(enable_gpio)) {
    metadata_.ring_buffer.number_of_channels = 4;
    metadata_.lanes_enable_mask[0] = 0xf;
    metadata_.dai.type = metadata::DaiType::Tdm1;
    metadata_.dai.number_of_channels = 4;
    metadata_.dai.bits_per_slot = 16;
    aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, region.GetMmioBuffer());
  }
};

TEST(AmlG12Tdm, InitializeTdm1Out) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<CodecTest>();
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
      fake_parent.get(), &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  mock.VerifyAll();
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, I2sOutCodecsStartedAndMuted) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecTest>();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto};

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, unused_mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  auto endpoints2 = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  ASSERT_OK(endpoints2.status_value());
  auto [local, remote] = *std::move(endpoints2);

  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  format.set_pcm_format(allocator, GetDefaultPcmFormat());
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

  // To make sure we have initialized in the controller driver make a sync call
  // (we know the controller is single threaded, initialization is completed if received a reply).
  auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
  ASSERT_OK(props.status());

  // Wait until codecs have received a SetGainState call.
  codec1->wait_for_set_gain_completion();
  codec2->wait_for_set_gain_completion();

  // Check we started (al least not stopped) both codecs and set them to muted.
  ASSERT_TRUE(codec1->started());
  ASSERT_TRUE(codec2->started());
  ASSERT_TRUE(codec1->muted());
  ASSERT_TRUE(codec2->muted());

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, I2sOutCodecsTurnOnDelay) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecTest>();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto};
  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, unused_mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  auto endpoints2 = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  ASSERT_OK(endpoints2.status_value());
  auto [local, remote] = *std::move(endpoints2);

  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  format.set_pcm_format(allocator, GetDefaultPcmFormat());
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

  auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
  ASSERT_OK(props.status());

  EXPECT_EQ(kTestTurnOnNsecs, props.value().properties.turn_on_delay());

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, I2sOutSetGainState) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecTest>();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto};

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, unused_mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  // Wait until codecs have received a SetGainState call.
  codec1->wait_for_set_gain_completion();
  codec2->wait_for_set_gain_completion();

  {
    {
      fidl::Arena allocator;
      // We start with agc false and muted true.
      audio_fidl::wire::GainState gain_state(allocator);
      gain_state.set_muted(true).set_agc_enabled(false).set_gain_db(kTestGain);
      // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
      (void)stream_client->SetGain(std::move(gain_state));
    }

    // Wait until codecs have received a SetGainState call.
    codec1->wait_for_set_gain_completion();
    codec2->wait_for_set_gain_completion();

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    // In this test we want to get the gain state anyways.
    auto gain_state = stream_client->WatchGainState();
    ASSERT_TRUE(gain_state.value().gain_state.has_agc_enabled());
    ASSERT_FALSE(gain_state.value().gain_state.agc_enabled());
    ASSERT_TRUE(gain_state.value().gain_state.muted());
    ASSERT_EQ(gain_state.value().gain_state.gain_db(), kTestGain);

    ASSERT_EQ(codec1->gain(), kTestGain + kTestDeltaGain);
    ASSERT_EQ(codec2->gain(), kTestGain);
    ASSERT_TRUE(codec1->muted());
    ASSERT_TRUE(codec2->muted());
  }

  {
    {
      fidl::Arena allocator;
      // We switch to agc true and muted false.
      audio_fidl::wire::GainState gain_state(allocator);
      gain_state.set_muted(false).set_agc_enabled(true).set_gain_db(kTestGain);
      // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
      (void)stream_client->SetGain(std::move(gain_state));
    }

    // Wait until codecs have received a SetGainState call.
    codec1->wait_for_set_gain_completion();
    codec2->wait_for_set_gain_completion();

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    // In this test we want to get the gain state anyways.
    auto gain_state = stream_client->WatchGainState();

    ASSERT_TRUE(gain_state.value().gain_state.has_agc_enabled());
    ASSERT_TRUE(gain_state.value().gain_state.agc_enabled());
    ASSERT_FALSE(gain_state.value().gain_state.muted());
    ASSERT_EQ(gain_state.value().gain_state.gain_db(), kTestGain);

    ASSERT_EQ(codec1->gain(), kTestGain + kTestDeltaGain);
    ASSERT_EQ(codec2->gain(), kTestGain);
    ASSERT_TRUE(codec1->muted());  // override_mute_ forces muted in the codec.
    ASSERT_TRUE(codec2->muted());  // override_mute_ forces muted in the codec.
  }

  {
    // Now we start the ring buffer so override_mute_ gets cleared.
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    format.set_pcm_format(allocator, GetDefaultPcmFormat());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    auto vmo = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetVmo(8192, 0);
    ASSERT_OK(vmo.status());
    auto start = fidl::WireCall<audio_fidl::RingBuffer>(local)->Start();
    ASSERT_OK(start.status());

    // Wait until codecs have received a SetGainState call.
    codec1->wait_for_set_gain_completion();
    codec2->wait_for_set_gain_completion();

    {
      fidl::Arena allocator;
      // Now we set gain again.
      // Change agc from last one, so the Watch below replies.
      audio_fidl::wire::GainState gain_state(allocator);
      gain_state.set_muted(false).set_agc_enabled(false).set_gain_db(kTestGain);
      // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
      (void)stream_client->SetGain(std::move(gain_state));
    }

    // Wait until codecs have received a SetGainState call.
    codec1->wait_for_set_gain_completion();
    codec2->wait_for_set_gain_completion();

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    // In this test we want to get the gain state anyways.
    auto gain_state = stream_client->WatchGainState();

    ASSERT_TRUE(gain_state.value().gain_state.has_agc_enabled());
    ASSERT_FALSE(gain_state.value().gain_state.agc_enabled());
    ASSERT_FALSE(gain_state.value().gain_state.muted());
    ASSERT_EQ(gain_state.value().gain_state.gain_db(), kTestGain);

    // We check the gain delta support in one codec.
    ASSERT_EQ(codec1->gain(), kTestGain + kTestDeltaGain);
    ASSERT_EQ(codec2->gain(), kTestGain);

    // And finally we check that we removed mute in the codecs.
    ASSERT_FALSE(codec1->muted());  // override_mute_ is cleared, we were able to set mute to false.
    ASSERT_FALSE(codec2->muted());  // override_mute_ is cleared, we were able to set mute to false.
  }

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, I2sOutOneCodecCantAgc) {
  struct CodecCantAgcTest : public CodecTest {
    explicit CodecCantAgcTest(zx_device_t* device) : CodecTest(device) {}
    GainFormat GetGainFormat() override {
      return {.min_gain = -10.f,
              .max_gain = 10.f,
              .gain_step = .5f,
              .can_mute = true,
              .can_agc = false};
    }
  };

  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecCantAgcTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecCantAgcTest>();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto};

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, unused_mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  auto props = stream_client->GetProperties();
  ASSERT_OK(props.status());

  EXPECT_TRUE(props.value().properties.can_mute());
  EXPECT_FALSE(props.value().properties.can_agc());

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, I2sOutOneCodecCantMute) {
  struct CodecCantMuteTest : public CodecTest {
    explicit CodecCantMuteTest(zx_device_t* device) : CodecTest(device) {}
    GainFormat GetGainFormat() override {
      return {.min_gain = -10.f,
              .max_gain = 10.f,
              .gain_step = .5f,
              .can_mute = false,
              .can_agc = true};
    }
  };

  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecCantMuteTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecCantMuteTest>();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto};

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, unused_mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  auto props = stream_client->GetProperties();
  ASSERT_OK(props.status());

  EXPECT_FALSE(props.value().properties.can_mute());
  EXPECT_TRUE(props.value().properties.can_agc());

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, I2sOutCodecsStop) {
  // Setup a system with 3 codecs.
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecTest>();
  auto codec2_proto = codec2->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev3 = fake_parent->GetLatestChild();
  auto codec3 = child_dev3->GetDeviceContext<CodecTest>();
  auto codec3_proto = codec3->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto, &codec3_proto};

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, unused_mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  // We stop the ring buffer and expect the codecs are stopped.
  auto endpoints2 = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  ASSERT_OK(endpoints2.status_value());
  auto [local, remote] = *std::move(endpoints2);
  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
  pcm_format.number_of_channels = 3;
  format.set_pcm_format(allocator, std::move(pcm_format));
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

  constexpr uint32_t kFramesRequested = 4096;
  auto vmo = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetVmo(kFramesRequested, 0);
  ASSERT_OK(vmo.status());

  auto start = fidl::WireCall<audio_fidl::RingBuffer>(local)->Start();
  ASSERT_OK(start.status());

  EXPECT_TRUE(codec1->started());
  EXPECT_TRUE(codec2->started());
  EXPECT_TRUE(codec3->started());

  auto stop = fidl::WireCall<audio_fidl::RingBuffer>(local)->Stop();
  ASSERT_OK(stop.status());

  EXPECT_FALSE(codec1->started());
  EXPECT_FALSE(codec2->started());
  EXPECT_FALSE(codec3->started());

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, I2sOutCodecsChannelsActive) {
  // Setup a system with 3 codecs.
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecTest>();
  auto codec2_proto = codec2->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev3 = fake_parent->GetLatestChild();
  auto codec3 = child_dev3->GetDeviceContext<CodecTest>();
  auto codec3_proto = codec3->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion unused_mock(regs.data(), sizeof(uint32_t), kRegSize);
  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto, &codec3_proto};

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, unused_mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  // We expect the codecs to start/stop.
  auto endpoints2 = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  ASSERT_OK(endpoints2.status_value());
  auto [local, remote] = *std::move(endpoints2);
  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
  pcm_format.number_of_channels = 3;
  format.set_pcm_format(allocator, std::move(pcm_format));
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

  constexpr uint32_t kFramesRequested = 4096;
  auto vmo = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetVmo(kFramesRequested, 0);
  ASSERT_OK(vmo.status());

  auto start1 = fidl::WireCall<audio_fidl::RingBuffer>(local)->Start();
  ASSERT_OK(start1.status());

  EXPECT_TRUE(codec1->started());
  EXPECT_TRUE(codec2->started());
  EXPECT_TRUE(codec3->started());

  auto stop1 = fidl::WireCall<audio_fidl::RingBuffer>(local)->Stop();
  ASSERT_OK(stop1.status());

  EXPECT_FALSE(codec1->started());
  EXPECT_FALSE(codec2->started());
  EXPECT_FALSE(codec3->started());

  // We now use set active channels to disable.
  auto active1 = fidl::WireCall<audio_fidl::RingBuffer>(local)->SetActiveChannels(0x5);
  ASSERT_OK(active1.status());

  auto start2 = fidl::WireCall<audio_fidl::RingBuffer>(local)->Start();
  ASSERT_OK(start2.status());

  EXPECT_TRUE(codec1->started());
  EXPECT_FALSE(codec2->started());  // Disabled via set active channels 0x05.
  EXPECT_TRUE(codec3->started());

  // We update active channels while started.
  auto active2 = fidl::WireCall<audio_fidl::RingBuffer>(local)->SetActiveChannels(0x2);
  ASSERT_OK(active2.status());

  EXPECT_FALSE(codec1->started());
  EXPECT_TRUE(codec2->started());  // Enabled via set active channels 0x02.
  EXPECT_FALSE(codec3->started());

  // We update active channels while started.
  auto active3 = fidl::WireCall<audio_fidl::RingBuffer>(local)->SetActiveChannels(0x0);
  ASSERT_OK(active3.status());

  EXPECT_FALSE(codec1->started());
  EXPECT_FALSE(codec2->started());  // Stopped via set active channels 0x00.
  EXPECT_FALSE(codec3->started());

  auto stop2 = fidl::WireCall<audio_fidl::RingBuffer>(local)->Stop();
  ASSERT_OK(stop2.status());

  EXPECT_FALSE(codec1->started());
  EXPECT_FALSE(codec2->started());
  EXPECT_FALSE(codec3->started());

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, I2sOutSetMclks) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecTest>();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // HW Initialize the MCLK pads. EE_AUDIO_MST_PAD_CTRL0.
  mock[0x01C].ExpectRead(0x00000000).ExpectWrite(0x00000002);  // MCLK C for PAD 0.

  // HW Initialize the MCLK pads. EE_AUDIO_MST_PAD_CTRL1.
  // Set 3 bits twice to MCLK C (2) and leave other configurations unchanged.
  mock[0x020].ExpectRead(0xffffffff).ExpectWrite(0xfafffaff);  // MCLK C for PAD 1.

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto};

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
  mock.VerifyAll();
}

TEST(AmlG12Tdm, I2sOutChangeRate96K) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev1 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev1);
  auto codec1 = child_dev1->GetDeviceContext<CodecTest>();
  auto codec1_proto = codec1->GetProto();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  auto codec2 = child_dev2->GetDeviceContext<CodecTest>();
  auto codec2_proto = codec2->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  // HW Initialize the MCLK pads. EE_AUDIO_MST_PAD_CTRL0.
  mock[0x01C].ExpectRead(0x00000000).ExpectWrite(0x00000002);  // MCLK C for PAD 0.

  // HW Initialize with 48kHz, set MCLK C CTRL.
  mock[0x00c].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x00c].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x00c].ExpectRead(0x00000000).ExpectWrite(0x84000009);  // Enabled, HIFI PLL, set div to 10.

  // HW Initialize with requested 48kHz, set MCLK C CTRL.
  mock[0x00c].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x00c].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x00c].ExpectRead(0x00000000).ExpectWrite(0x84000009);  // Enabled, HIFI PLL, set div to 10.

  // HW Initialize with requested 96kHz, set MCLK C CTRL.
  mock[0x00c].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x00c].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x00c].ExpectRead(0x00000000).ExpectWrite(0x84000004);  // Enabled, HIFI PLL, set div to 5.

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);
  std::vector<codec_protocol_t*> codec_protocols = {&codec1_proto, &codec2_proto};

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sOutTest>(
      fake_parent.get(), codec_protocols, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  // Default sets 48'000.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    format.set_pcm_format(allocator, GetDefaultPcmFormat());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
    ASSERT_OK(props.status());
  }
  // Changes to 96'000.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.frame_rate = 96'000;  // Change it from the default at 48kHz.
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    // To make sure we have initialized in the controller driver make a sync call
    // (we know the controller is single threaded, initialization is completed if received a reply).
    auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
    ASSERT_OK(props.status());
  }

  // To make sure we have changed the rate in the codec make a sync call requiring codec reply
  // (we know the codec is single threaded, rate change is completed if received a reply).
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)stream_client->SetGain(audio_fidl::wire::GainState{});

  // Check that we set the codec to the new rate.
  ASSERT_EQ(codec1->last_frame_rate(), 96'000);
  ASSERT_EQ(codec2->last_frame_rate(), 96'000);

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
  mock.VerifyAll();
}

TEST(AmlG12Tdm, PcmChangeRates) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<CodecTest>();
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);

  auto controller = audio::SimpleAudioStream::Create<AmlG12PcmOutTest>(
      fake_parent.get(), &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  AmlG12I2sOutTest* test_dev2 = child_dev2->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev2);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  // HW Initialize the MCLK pads. EE_AUDIO_MST_PAD_CTRL0.
  mock[0x01C].ExpectRead(0xffffffff).ExpectWrite(0xfffffffc);  // MCLK A for PAD 0.

  // HW Initialize with requested 48kHz, set MCLK A CTRL.
  mock[0x004].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x004].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x004].ExpectRead(0x00000000).ExpectWrite(0x84000027);  // Enabled, HIFI PLL, set div to 40.

  // HW Initialize with requested 96kHz, set MCLK A CTRL.
  mock[0x004].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x004].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x004].ExpectRead(0x00000000).ExpectWrite(0x84000013);  // Enabled, HIFI PLL, set div to 20.

  // HW Initialize with requested 16kHz, set MCLK A CTRL.
  mock[0x004].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x004].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x004].ExpectRead(0x00000000).ExpectWrite(0x84000077);  // Enabled, HIFI PLL, set div to 120.

  // HW Initialize with requested 8kHz, set MCLK A CTRL.
  mock[0x004].ExpectWrite(0x0400ffff);                         // HIFI PLL, and max div.
  mock[0x004].ExpectRead(0xffffffff).ExpectWrite(0x7fff0000);  // Disable, clear div.
  mock[0x004].ExpectRead(0x00000000).ExpectWrite(0x840000EF);  // Enabled, HIFI PLL, set div to 240.

  // Default sets 48'000 kHz.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));
  }

  // Sets 96'000 kHz.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.frame_rate = 96'000;  // Change it from the default at 48kHz.
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));
  }

  // Sets 16'000 kHz.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.frame_rate = 16'000;  // Change it from the default at 48kHz.
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
    ASSERT_OK(props.status());
  }

  // Sets 8'000 kHz.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.frame_rate = 8'000;  // Change it from the default at 48kHz.
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
    ASSERT_OK(props.status());
  }

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
  mock.VerifyAll();
}

TEST(AmlG12Tdm, EnableAndMuteChannelsPcm1Channel) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<CodecTest>();
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);

  auto controller = audio::SimpleAudioStream::Create<AmlG12PcmOutTest>(
      fake_parent.get(), &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  AmlG12I2sOutTest* test_dev2 = child_dev2->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev2);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  // Clear all muting. EE_AUDIO_TDMOUT_A_MUTE.
  mock[0x52c].ExpectWrite(0);  // TDMOUT MUTE0.
  mock[0x530].ExpectWrite(0);  // TDMOUT MUTE1.
  mock[0x534].ExpectWrite(0);  // TDMOUT MUTE2.
  mock[0x538].ExpectWrite(0);  // TDMOUT MUTE3.

  // Enable 1 channel per metadata_.lanes_enable_mask[0] in AmlG12PcmOutTest.
  // EE_AUDIO_TDMOUT_A_MASK.
  mock[0x50c].ExpectWrite(1);  // TDMOUT MASK0.
  mock[0x510].ExpectWrite(0);  // TDMOUT MASK1.
  mock[0x514].ExpectWrite(0);  // TDMOUT MASK2.
  mock[0x518].ExpectWrite(0);  // TDMOUT MASK3.

  // Nothing muted. EE_AUDIO_TDMOUT_A_MUTE.
  mock[0x52c].ExpectWrite(0);  // TDMOUT MUTE0.
  mock[0x530].ExpectWrite(0);  // TDMOUT MUTE1.
  mock[0x534].ExpectWrite(0);  // TDMOUT MUTE2.
  mock[0x538].ExpectWrite(0);  // TDMOUT MUTE3.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.number_of_channels = 4;
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
    ASSERT_OK(props.status());
  }

  mock.VerifyAll();
  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, EnableAndMuteChannelsTdm2Lanes) {
  auto fake_parent = MockDevice::FakeRootParent();

  struct AmlG12Tdm2LanesOutMuteTest : public AmlG12I2sOutTest {
    AmlG12Tdm2LanesOutMuteTest(zx_device_t* parent, codec_protocol_t* codec_protocol,
                               ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                               ddk::GpioProtocolClient enable_gpio)
        : AmlG12I2sOutTest(parent, codec_protocol, region, std::move(pdev),
                           std::move(enable_gpio)) {
      metadata_.ring_buffer.number_of_channels = 4;
      metadata_.lanes_enable_mask[0] = 0x3;
      metadata_.lanes_enable_mask[1] = 0x3;
      metadata_.dai.type = metadata::DaiType::Tdm1;
      metadata_.dai.bits_per_slot = 16;
      aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, region.GetMmioBuffer());
    }
  };

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<CodecTest>();
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);

  auto controller = audio::SimpleAudioStream::Create<AmlG12Tdm2LanesOutMuteTest>(
      fake_parent.get(), &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  AmlG12I2sOutTest* test_dev2 = child_dev2->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev2);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  // Clear all muting.
  mock[0x5ac].ExpectWrite(0);  // TDMOUT MUTE0.
  mock[0x5b0].ExpectWrite(0);  // TDMOUT MUTE1.
  mock[0x5b4].ExpectWrite(0);  // TDMOUT MUTE2.
  mock[0x5b8].ExpectWrite(0);  // TDMOUT MUTE3.

  // Enable 2 channels in lane 0 and 2 channels in lane 1.
  mock[0x58c].ExpectWrite(3);  // TDMOUT MASK0.
  mock[0x590].ExpectWrite(3);  // TDMOUT MASK1.
  mock[0x594].ExpectWrite(0);  // TDMOUT MASK2.
  mock[0x598].ExpectWrite(0);  // TDMOUT MASK3.

  // Nothing muted.
  mock[0x5ac].ExpectWrite(0);  // TDMOUT MUTE0.
  mock[0x5b0].ExpectWrite(0);  // TDMOUT MUTE1.
  mock[0x5b4].ExpectWrite(0);  // TDMOUT MUTE2.
  mock[0x5b8].ExpectWrite(0);  // TDMOUT MUTE3.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.number_of_channels = 4;
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
    ASSERT_OK(props.status());
  }

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
  mock.VerifyAll();
}

TEST(AmlG12Tdm, EnableAndMuteChannelsTdm1Lane) {
  auto fake_parent = MockDevice::FakeRootParent();

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<CodecTest>(fake_parent.get()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<CodecTest>();
  auto codec_proto = codec->GetProto();

  constexpr size_t kRegSize = S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  fbl::Array<ddk_mock::MockMmioReg> regs =
      fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
  ddk_mock::MockMmioRegRegion mock(regs.data(), sizeof(uint32_t), kRegSize);

  ddk::PDev unused_pdev;
  ddk::MockGpio enable_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);

  auto controller = audio::SimpleAudioStream::Create<AmlG12Tdm1OutTest>(
      fake_parent.get(), &codec_proto, mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);
  AmlG12I2sOutTest* test_dev2 = child_dev2->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev2);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  //
  // Configure and keep everything enabled.
  mock[0x5ac].ExpectWrite(0);  // TDMOUT MUTE0.
  mock[0x5b0].ExpectWrite(0);  // TDMOUT MUTE1.
  mock[0x5b4].ExpectWrite(0);  // TDMOUT MUTE2.
  mock[0x5b8].ExpectWrite(0);  // TDMOUT MUTE3.

  // Enable 4 channels in lane 0.
  mock[0x58c].ExpectWrite(0xf);  // TDMOUT MASK0.
  mock[0x590].ExpectWrite(0);    // TDMOUT MASK1.
  mock[0x594].ExpectWrite(0);    // TDMOUT MASK2.
  mock[0x598].ExpectWrite(0);    // TDMOUT MASK3.

  // Nothing muted.
  mock[0x5ac].ExpectWrite(0);  // TDMOUT MUTE0.
  mock[0x5b0].ExpectWrite(0);  // TDMOUT MUTE1.
  mock[0x5b4].ExpectWrite(0);  // TDMOUT MUTE2.
  mock[0x5b8].ExpectWrite(0);  // TDMOUT MUTE3.
  {
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.number_of_channels = 4;
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    // To make sure call initialization in the controller, make a sync call
    // (we know the controller is single threaded, init completed if received a reply).
    auto props = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetProperties();
    ASSERT_OK(props.status());
  }

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  enable_gpio.VerifyAndClear();
  mock.VerifyAll();
}

struct AmlG12I2sInTest : public AmlG12TdmStream {
  AmlG12I2sInTest(zx_device_t* parent, ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                  ddk::GpioProtocolClient enable_gpio)
      : AmlG12TdmStream(parent, true, std::move(pdev), std::move(enable_gpio)) {
    metadata_.is_input = true;
    metadata_.mClockDivFactor = 10;
    metadata_.sClockDivFactor = 25;
    metadata_.ring_buffer.number_of_channels = 2;
    metadata_.dai.number_of_channels = 2;
    metadata_.lanes_enable_mask[0] = 3;
    metadata_.bus = metadata::AmlBus::TDM_C;
    metadata_.version = metadata::AmlVersion::kS905D2G;
    metadata_.dai.type = metadata::DaiType::I2s;
    metadata_.dai.bits_per_sample = 16;
    metadata_.dai.bits_per_slot = 32;
    metadata_.codecs.number_of_codecs = 0;
    aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, region.GetMmioBuffer());
  }

  zx_status_t Init() __TA_REQUIRES(domain_token()) override {
    SimpleAudioStream::SupportedFormat format = {};
    format.range.min_channels = 2;
    format.range.max_channels = 2;
    format.range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
    format.range.min_frames_per_second = 48'000;
    format.range.max_frames_per_second = 96'000;
    format.range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
    supported_formats_.push_back(std::move(format));

    fifo_depth_ = 16;

    cur_gain_state_ = {};

    SetInitialPlugState(AUDIO_PDNF_CAN_NOTIFY);

    snprintf(device_name_, sizeof(device_name_), "Testy Device");
    snprintf(mfr_name_, sizeof(mfr_name_), "Testy Inc");
    snprintf(prod_name_, sizeof(prod_name_), "Testy McTest");

    unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_SPEAKERS;

    constexpr uint64_t channels_to_use = 0x03;
    return aml_audio_->InitHW(metadata_, channels_to_use, 48'000);
  }
};

struct AmlG12PcmInTest : public AmlG12I2sInTest {
  AmlG12PcmInTest(zx_device_t* parent, ddk_mock::MockMmioRegRegion& region, ddk::PDev pdev,
                  ddk::GpioProtocolClient enable_gpio)
      : AmlG12I2sInTest(parent, region, std::move(pdev), std::move(enable_gpio)) {
    metadata_.ring_buffer.number_of_channels = 1;
    metadata_.dai.number_of_channels = 1;
    metadata_.lanes_enable_mask[0] = 1;
    metadata_.dai.type = metadata::DaiType::Tdm1;
    metadata_.dai.bits_per_slot = 16;
    metadata_.dai.sclk_on_raising = true;
    aml_audio_ = std::make_unique<AmlTdmConfigDevice>(metadata_, region.GetMmioBuffer());
  }
};

TEST(AmlG12Tdm, InitializeI2sIn) {
  auto fake_parent = MockDevice::FakeRootParent();

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

  auto controller = audio::SimpleAudioStream::Create<AmlG12I2sInTest>(
      fake_parent.get(), mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  mock.VerifyAll();
  enable_gpio.VerifyAndClear();
}

TEST(AmlG12Tdm, InitializePcmIn) {
  auto fake_parent = MockDevice::FakeRootParent();

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

  auto controller = audio::SimpleAudioStream::Create<AmlG12PcmInTest>(
      fake_parent.get(), mock, unused_pdev, enable_gpio.GetProto());
  auto* child_dev2 = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev2);

  child_dev2->UnbindOp();
  EXPECT_TRUE(child_dev2->UnbindReplyCalled());
  mock.VerifyAll();
  enable_gpio.VerifyAndClear();
}

class FakeMmio {
 public:
  FakeMmio() {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegCount);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t), kRegCount);
  }

  fake_pdev::FakePDev::MmioInfo mmio_info() { return {.offset = reinterpret_cast<size_t>(this)}; }

  fdf::MmioBuffer mmio() { return fdf::MmioBuffer(mmio_->GetMmioBuffer()); }
  ddk_fake::FakeMmioReg& AtIndex(size_t ix) { return regs_[ix]; }

 private:
  static constexpr size_t kRegCount =
      S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  std::unique_ptr<ddk_fake::FakeMmioReg[]> regs_;
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> mmio_;
};

class TestAmlG12TdmStream : public AmlG12TdmStream {
 public:
  explicit TestAmlG12TdmStream(zx_device_t* parent, ddk::PDev pdev,
                               const ddk::GpioProtocolClient enable_gpio)
      : AmlG12TdmStream(parent, false, std::move(pdev), std::move(enable_gpio)) {}
  bool AllowNonContiguousRingBuffer() override { return true; }
  inspect::Inspector& inspect() { return AmlG12TdmStream::inspect(); }
};

metadata::AmlConfig GetDefaultMetadata() {
  metadata::AmlConfig metadata = {};
  metadata.is_input = false;
  metadata.mClockDivFactor = 10;
  metadata.sClockDivFactor = 25;
  metadata.ring_buffer.number_of_channels = 2;
  metadata.dai.number_of_channels = 2;
  metadata.lanes_enable_mask[0] = 3;
  metadata.bus = metadata::AmlBus::TDM_C;
  metadata.version = metadata::AmlVersion::kS905D2G;
  metadata.dai.type = metadata::DaiType::I2s;
  metadata.dai.bits_per_sample = 16;
  metadata.dai.bits_per_slot = 32;
  return metadata;
}

struct AmlG12TdmTest : public inspect::InspectTestHelper, public zxtest::Test {
  void SetUp() override {
    pdev_.set_mmio(0, mmio_.mmio_info());
    pdev_.UseFakeBti();
    zx::interrupt irq;
    ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));
    pdev_.set_interrupt(0, std::move(irq));
  }

  void CreateRingBuffer() {
    auto fake_parent = MockDevice::FakeRootParent();
    auto metadata = GetDefaultMetadata();
    fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

    ddk::GpioProtocolClient unused_gpio;

    auto controller = audio::SimpleAudioStream::Create<TestAmlG12TdmStream>(
        fake_parent.get(), pdev_.proto(), unused_gpio);
    auto* child_dev = fake_parent->GetLatestChild();
    ASSERT_NOT_NULL(child_dev);
    AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
    std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
    binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
        loop.dispatcher(), std::move(endpoints->server), test_dev);
    loop.StartThread("test-server");

    auto stream_client = GetStreamClient(std::move(endpoints->client));
    ASSERT_TRUE(stream_client.is_valid());

    auto endpoints2 = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints2.status_value());
    auto [local, remote] = *std::move(endpoints2);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    format.set_pcm_format(allocator, GetDefaultPcmFormat());
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    child_dev->UnbindOp();
    EXPECT_TRUE(child_dev->UnbindReplyCalled());
  }

  void TestRingBufferSize(uint8_t number_of_channels, uint32_t frames_req,
                          uint32_t frames_expected) {
    auto fake_parent = MockDevice::FakeRootParent();
    auto metadata = GetDefaultMetadata();
    metadata.ring_buffer.number_of_channels = number_of_channels;
    metadata.lanes_enable_mask[0] = (1 << number_of_channels) - 1;
    fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

    ddk::GpioProtocolClient unused_gpio;

    auto controller = audio::SimpleAudioStream::Create<TestAmlG12TdmStream>(
        fake_parent.get(), pdev_.proto(), unused_gpio);
    auto* child_dev = fake_parent->GetLatestChild();
    ASSERT_NOT_NULL(child_dev);
    AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
    std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
    binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
        loop.dispatcher(), std::move(endpoints->server), test_dev);
    loop.StartThread("test-server");

    auto stream_client = GetStreamClient(std::move(endpoints->client));
    ASSERT_TRUE(stream_client.is_valid());

    auto endpoints2 = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints2.status_value());
    auto [local, remote] = *std::move(endpoints2);

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.number_of_channels = number_of_channels;
    format.set_pcm_format(allocator, std::move(pcm_format));
    // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
    (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

    auto vmo = fidl::WireCall<audio_fidl::RingBuffer>(local)->GetVmo(frames_req, 0);
    ASSERT_OK(vmo.status());
    ASSERT_EQ(vmo->value()->num_frames, frames_expected);

    child_dev->UnbindOp();
    EXPECT_TRUE(child_dev->UnbindReplyCalled());
  }

  void TestAttributes() {
    auto fake_parent = MockDevice::FakeRootParent();
    metadata::AmlConfig metadata = GetDefaultMetadata();
    metadata.ring_buffer.frequency_ranges[0].min_frequency = 40;
    metadata.ring_buffer.frequency_ranges[0].max_frequency = 200;
    metadata.ring_buffer.frequency_ranges[1].min_frequency = 200;
    metadata.ring_buffer.frequency_ranges[1].max_frequency = 20'000;
    fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

    ddk::GpioProtocolClient unused_gpio;

    auto controller = audio::SimpleAudioStream::Create<TestAmlG12TdmStream>(
        fake_parent.get(), pdev_.proto(), unused_gpio);
    auto* child_dev = fake_parent->GetLatestChild();
    ASSERT_NOT_NULL(child_dev);
    AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
    std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
    binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
        loop.dispatcher(), std::move(endpoints->server), test_dev);
    loop.StartThread("test-server");

    auto stream_client = GetStreamClient(std::move(endpoints->client));
    ASSERT_TRUE(stream_client.is_valid());

    // Check channels attributes.
    auto supported = stream_client->GetSupportedFormats();
    ASSERT_OK(supported.status());

    auto& pcm_supported_formats0 = supported.value().supported_formats[0].pcm_supported_formats();
    ASSERT_EQ(pcm_supported_formats0.frame_rates()[0], 8'000);
    auto& attributes0 = pcm_supported_formats0.channel_sets()[0].attributes();
    ASSERT_EQ(attributes0.count(), 2);
    ASSERT_EQ(attributes0[0].min_frequency(), 40);
    ASSERT_EQ(attributes0[0].max_frequency(), 200);
    ASSERT_EQ(attributes0[1].min_frequency(), 200);
    ASSERT_EQ(attributes0[1].max_frequency(), 20'000);

    auto& pcm_supported_formats1 = supported.value().supported_formats[1].pcm_supported_formats();
    ASSERT_EQ(pcm_supported_formats1.frame_rates()[0], 16'000);
    auto& attributes1 = pcm_supported_formats1.channel_sets()[0].attributes();
    ASSERT_EQ(attributes1.count(), 2);
    ASSERT_EQ(attributes1[0].min_frequency(), 40);
    ASSERT_EQ(attributes1[0].max_frequency(), 200);
    ASSERT_EQ(attributes1[1].min_frequency(), 200);
    ASSERT_EQ(attributes1[1].max_frequency(), 20'000);

    child_dev->UnbindOp();
    EXPECT_TRUE(child_dev->UnbindReplyCalled());
  }

  FakeMmio mmio_;
  fake_pdev::FakePDev pdev_;
};

// With 16 bits samples, frame size is 2 x number of channels bytes.
// Frames returned are rounded to HW buffer alignment (8 bytes) and frame size.
TEST_F(AmlG12TdmTest, RingBufferSize1) { TestRingBufferSize(2, 1, 2); }  // Rounded to HW buffer.
TEST_F(AmlG12TdmTest, RingBufferSize2) { TestRingBufferSize(2, 3, 4); }  // Rounded to HW buffer.
TEST_F(AmlG12TdmTest, RingBufferSize3) { TestRingBufferSize(3, 1, 4); }  // Rounded to both.
TEST_F(AmlG12TdmTest, RingBufferSize4) { TestRingBufferSize(3, 3, 4); }  // Rounded to both.
TEST_F(AmlG12TdmTest, RingBufferSize5) { TestRingBufferSize(8, 1, 1); }  // Rounded to frame size.
TEST_F(AmlG12TdmTest, RingBufferSize6) { TestRingBufferSize(8, 3, 3); }  // Rounded to frame size.

TEST_F(AmlG12TdmTest, Attributes) { TestAttributes(); }

TEST_F(AmlG12TdmTest, Rate) {
  uint32_t mclk_ctrl = 0;
  uint32_t sclk_ctrl = 0;
  mmio_.AtIndex(0x3).SetWriteCallback([&mclk_ctrl](uint64_t value) { mclk_ctrl = value; });
  mmio_.AtIndex(0x14).SetWriteCallback([&sclk_ctrl](uint64_t value) { sclk_ctrl = value; });
  CreateRingBuffer();                // Defaults to 48kHz rate.
  ASSERT_EQ(0x84000009, mclk_ctrl);  // clkdiv = 9 for 48kHz rate.
  ASSERT_EQ(0xC1807C3F, sclk_ctrl);  // enabled, 24 sdiv, 31 lrduty, 63 lrdiv for 48kHz rate.
}

TEST_F(AmlG12TdmTest, Inspect) {
  auto fake_parent = MockDevice::FakeRootParent();
  auto metadata = GetDefaultMetadata();
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  ddk::GpioProtocolClient unused_gpio;

  auto controller = audio::SimpleAudioStream::Create<TestAmlG12TdmStream>(
      fake_parent.get(), pdev_.proto(), unused_gpio);
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  AmlG12I2sOutTest* test_dev = child_dev->GetDeviceContext<AmlG12I2sOutTest>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<audio_fidl::StreamConfigConnector>();
  std::optional<fidl::ServerBindingRef<audio_fidl::StreamConfigConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<audio_fidl::StreamConfigConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto stream_client = GetStreamClient(std::move(endpoints->client));
  ASSERT_TRUE(stream_client.is_valid());

  auto endpoints2 = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  ASSERT_OK(endpoints2.status_value());
  auto [local, remote] = *std::move(endpoints2);

  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  format.set_pcm_format(allocator, GetDefaultPcmFormat());
  // TODO(fxbug.dev/97955) Consider handling the error instead of ignoring it.
  (void)stream_client->CreateRingBuffer(std::move(format), std::move(remote));

  // Check inspect state.
  ASSERT_NO_FATAL_FAILURE(ReadInspect(test_dev->inspect().DuplicateVmo()));
  auto* simple_audio = hierarchy().GetByPath({"simple_audio_stream"});
  ASSERT_TRUE(simple_audio);
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_audio->node(), "state", inspect::StringPropertyValue("created")));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(hierarchy().node(), "status_time", inspect::IntPropertyValue(0)));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(hierarchy().node(), "dma_status", inspect::UintPropertyValue(0)));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(hierarchy().node(), "tdm_status", inspect::UintPropertyValue(0)));

  child_dev->UnbindOp();
  EXPECT_TRUE(child_dev->UnbindReplyCalled());
}

}  // namespace audio::aml_g12

// Redefine PDevMakeMmioBufferWeak per the recommendation in pdev.h.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* test_harness = reinterpret_cast<audio::aml_g12::FakeMmio*>(pdev_mmio.offset);
  mmio->emplace(test_harness->mmio());
  return ZX_OK;
}
