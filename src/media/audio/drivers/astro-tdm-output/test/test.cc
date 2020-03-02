// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <audio-proto/audio-proto.h>
#include <audio-utils/audio-output.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

#include "../audio-stream-out.h"

namespace audio {
namespace astro {

using ::llcpp::fuchsia::hardware::audio::Device;

struct Tas27xxGoodInitTest : Tas27xx {
  Tas27xxGoodInitTest(ddk::I2cChannel i2c) : Tas27xx(std::move(i2c)) {}
  zx_status_t Init(uint32_t rate) override { return ZX_OK; }
};

struct Tas27xxBadInitTest : Tas27xx {
  Tas27xxBadInitTest(ddk::I2cChannel i2c) : Tas27xx(std::move(i2c)) {}
  zx_status_t Init(uint32_t rate) override { return ZX_ERR_INTERNAL; }
};

struct AstroAudioStreamOutCodecInitTest : public AstroAudioStreamOut {
  AstroAudioStreamOutCodecInitTest(zx_device_t* parent, std::unique_ptr<Tas27xx> codec,
                                   const gpio_protocol_t* audio_enable_gpio)
      : AstroAudioStreamOut(parent) {
    codec_ = std::move(codec);
    audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
  }

  zx_status_t InitPDev() override {
    return InitCodec();  // Only init the Codec, no the rest of the audio stream initialization.
  }
  void ShutdownHook() override {}  // Do not perform shutdown since we don't initialize in InitPDev.
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

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);

  auto codec = std::make_unique<Tas27xxGoodInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<AstroAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codec), audio_enable_gpio.GetProto());

  ASSERT_NOT_NULL(server);
  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
  server->DdkRelease();
}

TEST(AstroAudioStreamOutTest, CodecInitBad) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);

  auto codec = std::make_unique<Tas27xxBadInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<AstroAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codec), audio_enable_gpio.GetProto());

  ASSERT_NULL(server);
  // Not tester.Ok() since the we don't add the device.
  audio_enable_gpio.VerifyAndClear();
}

TEST(AstroAudioStreamOutTest, ChangeRate96K) {
  static constexpr uint32_t kTestFrameRate1 = 48000;
  static constexpr uint32_t kTestFrameRate2 = 96000;
  static constexpr uint8_t kTestNumberOfChannels = 2;
  static constexpr uint32_t kTestFifoDepth = 16;
  struct CodecRate96KTest : Tas27xx {
    CodecRate96KTest(ddk::I2cChannel i2c) : Tas27xx(std::move(i2c)) {}
    zx_status_t Init(uint32_t rate) override {
      last_rate_requested_ = rate;
      return ZX_OK;
    }
    zx_status_t SetGain(float gain) override { return ZX_OK; }
    uint32_t last_rate_requested_ = 0;
  };
  struct Rate96KTest : public AstroAudioStreamOut {
    Rate96KTest(zx_device_t* parent, std::unique_ptr<Tas27xx> codec,
                const gpio_protocol_t* audio_enable_gpio)
        : AstroAudioStreamOut(parent) {
      codec_ = std::move(codec);
      audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
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

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);

  auto raw_codec = new CodecRate96KTest(mock_i2c.GetProto());
  auto codec = std::unique_ptr<CodecRate96KTest>(raw_codec);
  auto server = audio::SimpleAudioStream::Create<Rate96KTest>(
      fake_ddk::kFakeParent, std::move(codec), audio_enable_gpio.GetProto());
  ASSERT_NOT_NULL(server);

  Device::SyncClient client(std::move(tester.FidlClient()));
  Device::ResultOf::GetChannel channel_wrap = client.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  // After we get the channel we use audio::utils serialization until we convert to FIDL.
  auto channel_client = audio::utils::AudioOutput::Create(1);
  channel_client->SetStreamChannel(std::move(channel_wrap->channel));

  audio_sample_format_t format = AUDIO_SAMPLE_FORMAT_16BIT;
  ASSERT_OK(channel_client->SetFormat(kTestFrameRate2, kTestNumberOfChannels, format));
  ASSERT_EQ(raw_codec->last_rate_requested_, kTestFrameRate2);

  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

}  // namespace astro
}  // namespace audio
