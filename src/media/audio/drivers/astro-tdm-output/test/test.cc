// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <audio-proto/audio-proto.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

#include "../audio-stream-out.h"

namespace audio {
namespace astro {

static constexpr uint32_t kTestFrameRate1 = 48000;
static constexpr uint32_t kTestFrameRate2 = 96000;
static constexpr uint8_t kTestNumberOfChannels = 2;
static constexpr uint32_t kTestFifoDepth = 16;

using ::llcpp::fuchsia::hardware::audio::Device;

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

struct Tas27xxInitTest : Tas27xx {
  Tas27xxInitTest(ddk::I2cChannel i2c, ddk::GpioProtocolClient ena, ddk::GpioProtocolClient fault)
      : Tas27xx(std::move(i2c), std::move(ena), std::move(fault), true, true) {}
};

struct AstroAudioStreamOutCodecInitTest : public AstroAudioStreamOut {
  AstroAudioStreamOutCodecInitTest(zx_device_t* parent, std::unique_ptr<Tas27xx> codec)
      : AstroAudioStreamOut(parent) {
    codec_ = std::move(codec);
    tdm_config_.type = metadata::TdmType::I2s;
    tdm_config_.codec = metadata::Codec::Tas2770;
  }

  zx_status_t InitPDev() override {
    return codec_->Init(
        48000);  // Only init the Codec, not the rest of the audio stream initialization.
  }
  void ShutdownHook() override { codec_->HardwareShutdown(); }
};

struct AmlTdmDeviceTest : public AmlTdmDevice {
  static std::unique_ptr<AmlTdmDeviceTest> Create() {
    constexpr size_t n_registers = 4096;  // big enough.
    static fbl::Array<ddk_mock::MockMmioReg> unused_mocks =
        fbl::Array(new ddk_mock::MockMmioReg[n_registers], n_registers);
    static ddk_mock::MockMmioRegRegion unused_region(unused_mocks.data(), sizeof(uint32_t),
                                                     n_registers);
    return std::make_unique<AmlTdmDeviceTest>(unused_region.GetMmioBuffer(), HIFI_PLL, TDM_OUT_C,
                                              FRDDR_A, MCLK_C, 0, AmlVersion::kS905D2G);
  }
  AmlTdmDeviceTest(ddk::MmioBuffer mmio, ee_audio_mclk_src_t clk_src, aml_tdm_out_t tdm,
                   aml_frddr_t frddr, aml_tdm_mclk_t mclk, uint32_t fifo_depth, AmlVersion version)
      : AmlTdmDevice(std::move(mmio), clk_src, tdm, frddr, mclk, fifo_depth, version) {}
  void Initialize() override {}
  void Shutdown() override {}
};

TEST(AstroAudioStreamOutTest, CodecInitGood) {
  fake_ddk::Bind tester;

  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);

  mock_i2c::MockI2c mock_i2c;
  mock_i2c
      .ExpectWriteStop({0x01, 0x01})  // sw reset
      .ExpectWriteStop({0x02, 0x01})  // Muted
      .ExpectWriteStop({0x3c, 0x10})  // CLOCK_CFG
      .ExpectWriteStop({0x0a, 0x07})  // SetRate
      .ExpectWriteStop({0x0c, 0x12})  // TDM_CFG2
      .ExpectWriteStop({0x0e, 0x02})  // TDM_CFG4
      .ExpectWriteStop({0x0f, 0x44})  // TDM_CFG5
      .ExpectWriteStop({0x10, 0x40})  // TDM_CFG6
      .ExpectWrite({0x24})
      .ExpectReadStop({0x00})  // INT_LTCH0
      .ExpectWrite({0x25})
      .ExpectReadStop({0x00})  // INT_LTCH1
      .ExpectWrite({0x26})
      .ExpectReadStop({0x00})  // INT_LTCH2
      .ExpectWriteStop({0x20, 0xf8})
      .ExpectWriteStop({0x21, 0xff})
      .ExpectWriteStop({0x30, 0x01})
      .ExpectWrite({0x05})
      .ExpectReadStop({0x00});  // GetGain

  ddk::MockGpio mock_ena;
  ddk::MockGpio mock_fault;
  mock_ena.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1).ExpectWrite(ZX_OK, 0);
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  auto codec = std::make_unique<Tas27xxInitTest>(mock_i2c.GetProto(), mock_ena.GetProto(),
                                                 mock_fault.GetProto());
  auto server = audio::SimpleAudioStream::Create<AstroAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codec));

  ASSERT_NOT_NULL(server);
  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
  mock_ena.VerifyAndClear();
  mock_i2c.VerifyAndClear();
  mock_fault.VerifyAndClear();
  server->DdkRelease();
}

TEST(AstroAudioStreamOutTest, CodecInitBad) {
  fake_ddk::Bind tester;

  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWriteStop({0x01, 0x01}, ZX_ERR_TIMED_OUT);  // sw reset

  ddk::MockGpio mock_ena;
  ddk::MockGpio mock_fault;
  mock_ena.ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 1).ExpectWrite(ZX_OK, 0).ExpectWrite(ZX_OK, 0);

  auto codec = std::make_unique<Tas27xxInitTest>(mock_i2c.GetProto(), mock_ena.GetProto(),
                                                 mock_fault.GetProto());
  auto server = audio::SimpleAudioStream::Create<AstroAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codec));

  ASSERT_NULL(server);
  // Not tester.Ok() since the we don't add the device.
  mock_ena.VerifyAndClear();
  mock_i2c.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

TEST(AstroAudioStreamOutTest, ChangeRate96K) {
  struct CodecRate96KTest : Tas27xx {
    CodecRate96KTest(ddk::I2cChannel i2c, ddk::GpioProtocolClient ena,
                     ddk::GpioProtocolClient fault)
        : Tas27xx(std::move(i2c), std::move(ena), std::move(fault), false, false) {}
  };

  struct Rate96KTest : public AstroAudioStreamOut {
    Rate96KTest(zx_device_t* parent, std::unique_ptr<Tas27xx> codec) : AstroAudioStreamOut(parent) {
      codec_ = std::move(codec);
      aml_audio_ = AmlTdmDeviceTest::Create();
    }
    zx_status_t Init() __TA_REQUIRES(domain_token()) override {
      audio_stream_format_range_t range;
      range.min_channels = kTestNumberOfChannels;
      range.max_channels = kTestNumberOfChannels;
      range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
      range.min_frames_per_second = kTestFrameRate1;
      range.max_frames_per_second = kTestFrameRate2;
      range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
      supported_formats_.push_back(range);

      fifo_depth_ = kTestFifoDepth;

      cur_gain_state_ = {};

      SetInitialPlugState(AUDIO_PDNF_CAN_NOTIFY);

      snprintf(device_name_, sizeof(device_name_), "test-audio-in");
      snprintf(mfr_name_, sizeof(mfr_name_), "Bike Sheds, Inc.");
      snprintf(prod_name_, sizeof(prod_name_), "testy_mctestface");

      unique_id_ = AUDIO_STREAM_UNIQUE_ID_BUILTIN_MICROPHONE;

      return ZX_OK;
    }

    bool init_hw_called_ = false;
  };

  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;

  mock_i2c
      .ExpectWriteStop({0x02, 0x0e})   // Stopped no I/V sense.
      .ExpectWriteStop({0x02, 0x0c});  // Started no I/V sense.

  ddk::MockGpio enable_gpio;
  ddk::MockGpio fault_gpio;
  enable_gpio.ExpectWrite(ZX_OK, 0);

  auto raw_codec =
      new CodecRate96KTest(mock_i2c.GetProto(), enable_gpio.GetProto(), fault_gpio.GetProto());
  auto codec = std::unique_ptr<CodecRate96KTest>(raw_codec);
  auto server =
      audio::SimpleAudioStream::Create<Rate96KTest>(fake_ddk::kFakeParent, std::move(codec));
  ASSERT_NOT_NULL(server);

  Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));
  audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
  pcm_format.frame_rate = kTestFrameRate2;  // Change it from the default at 48kHz.
  fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
  auto builder = audio_fidl::Format::UnownedBuilder();
  builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
  client.CreateRingBuffer(builder.build(), std::move(remote));

  // To make sure we have initialized in the server make a sync call
  // (we know the server is single threaded, initialization is completed if received a reply).
  auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
  ASSERT_OK(props.status());

  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
  enable_gpio.VerifyAndClear();
  mock_i2c.VerifyAndClear();
  server->DdkRelease();
}

}  // namespace astro
}  // namespace audio
