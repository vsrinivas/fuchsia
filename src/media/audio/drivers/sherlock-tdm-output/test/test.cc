// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <audio-utils/audio-output.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

#include "../audio-stream-out.h"

namespace audio {
namespace sherlock {

using ::llcpp::fuchsia::hardware::audio::Device;

// TODO(46617): This test is valid for Astro and Nelson once AMLogic audio drivers are unified.
struct Tas5720GoodInitTest : Tas5720 {
  Tas5720GoodInitTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
  zx_status_t Init(std::optional<uint8_t> slot, uint32_t rate) override { return ZX_OK; }
  zx_status_t SetGain(float gain) override { return ZX_OK; }
};

struct Tas5720BadInitTest : Tas5720 {
  Tas5720BadInitTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
  zx_status_t Init(std::optional<uint8_t> slot, uint32_t rate) override { return ZX_ERR_INTERNAL; }
  // Normally SetGain would not be called after a bad Init, but we fake continuing a bad
  // Init in the LibraryShutdwonOnInitWithError test, so we add a no-op SetGain anyways.
  zx_status_t SetGain(float gain) override { return ZX_OK; }
};

struct Tas5720SomeBadInitTest : Tas5720 {
  Tas5720SomeBadInitTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
  zx_status_t Init(std::optional<uint8_t> slot, uint32_t rate) override {
    if (slot.value() == 0) {
      return ZX_OK;
    } else {
      return ZX_ERR_INTERNAL;
    }
  }
  zx_status_t SetGain(float gain) override {
    return ZX_OK;
  }  // Gains work since not all Inits fail.
};

struct SherlockAudioStreamOutCodecInitTest : public SherlockAudioStreamOut {
  SherlockAudioStreamOutCodecInitTest(zx_device_t* parent,
                                      fbl::Array<std::unique_ptr<Tas5720>> codecs,
                                      const gpio_protocol_t* audio_enable_gpio)
      : SherlockAudioStreamOut(parent) {
    codecs_ = std::move(codecs);
    audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
  }

  zx_status_t InitPdev() TA_REQ(domain_token()) override {
    return InitCodecs();  // Only init the Codec, not the rest of the audio stream initialization.
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
  void Initialize() override { initialize_called_++; }
  void Shutdown() override { shutdown_called_++; }
  size_t initialize_called_ = 0;
  size_t shutdown_called_ = 0;
};

TEST(SherlockAudioStreamOutTest, CodecInitGood) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<SherlockAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());

  ASSERT_NOT_NULL(server);
  server->DdkUnbindDeprecated();
  server->DdkRelease();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
}

TEST(SherlockAudioStreamOutTest, CodecInitBad) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720BadInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720BadInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720BadInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<SherlockAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());

  ASSERT_NULL(server);
  // Not tester.Ok() since the we don't add the device.
  audio_enable_gpio.VerifyAndClear();
}

TEST(SherlockAudioStreamOutTest, CodecInitOnlySomeBad) {
  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720SomeBadInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720SomeBadInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720SomeBadInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<SherlockAudioStreamOutCodecInitTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());

  ASSERT_NULL(server);
  // Not tester.Ok() since the we don't add the device.
  audio_enable_gpio.VerifyAndClear();
}

TEST(SherlockAudioStreamOutTest, LibraryShutdwonOnInitNormal) {
  struct LibInitTest : public SherlockAudioStreamOut {
    LibInitTest(zx_device_t* parent, fbl::Array<std::unique_ptr<Tas5720>> codecs,
                const gpio_protocol_t* audio_enable_gpio)
        : SherlockAudioStreamOut(parent) {
      codecs_ = std::move(codecs);
      audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
      aml_audio_ = AmlTdmDeviceTest::Create();
    }
    size_t LibraryInitialized() {
      AmlTdmDeviceTest* test_aml_audio = static_cast<AmlTdmDeviceTest*>(aml_audio_.get());
      return test_aml_audio->initialize_called_;
    }
    size_t LibraryShutdown() {
      AmlTdmDeviceTest* test_aml_audio = static_cast<AmlTdmDeviceTest*>(aml_audio_.get());
      return test_aml_audio->shutdown_called_;
    }

    zx_status_t InitPdev() TA_REQ(domain_token()) override {
      return InitHW();  // Only init the HW, not the rest of the audio stream initialization.
    }
  };

  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);  // As part of regular init.
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);  // As part of unbind calling the ShutdownHook.

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<LibInitTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());
  ASSERT_NOT_NULL(server);

  // We test that we shutdown as part of unbind calling the ShutdownHook.
  ASSERT_EQ(server->LibraryShutdown(), 1);
  ASSERT_EQ(server->LibraryInitialized(), 1);
  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
}

TEST(SherlockAudioStreamOutTest, LibraryShutdwonOnInitWithError) {
  struct LibInitTest : public SherlockAudioStreamOut {
    LibInitTest(zx_device_t* parent, fbl::Array<std::unique_ptr<Tas5720>> codecs,
                const gpio_protocol_t* audio_enable_gpio)
        : SherlockAudioStreamOut(parent) {
      codecs_ = std::move(codecs);
      audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
      aml_audio_ = AmlTdmDeviceTest::Create();
    }
    bool LibraryInitialized() {
      AmlTdmDeviceTest* test_aml_audio = static_cast<AmlTdmDeviceTest*>(aml_audio_.get());
      return test_aml_audio->initialize_called_;
    }
    bool LibraryShutdown() {
      AmlTdmDeviceTest* test_aml_audio = static_cast<AmlTdmDeviceTest*>(aml_audio_.get());
      return test_aml_audio->shutdown_called_;
    }

    zx_status_t InitPdev() TA_REQ(domain_token()) override {
      auto status = InitHW();  // Only init the HW, not the rest of the audio stream initialization.
      ZX_ASSERT(status != ZX_OK);
      return ZX_OK;  // We lie here so we can check for the library shutdown.
    }
    // Do not perform shutdown, we want to test a codec error that have similar outcome.
    void ShutdownHook() override {}
  };

  fake_ddk::Bind tester;

  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);  // Once we fail with a bad init (below) we disable,
                                            // not due to ShutdownHook (disabled above).

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720BadInitTest>(mock_i2c.GetProto());  // This the bad init.
  auto server = audio::SimpleAudioStream::Create<LibInitTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());
  ASSERT_NOT_NULL(server);  // We make it ok in InitPdev above.

  // We test that we shutdown because the codec fails, not due to ShutdownHook (disabled above).
  ASSERT_EQ(server->LibraryShutdown(), 1);
  // We test that we dont't call initialize due to the bad codec init.
  ASSERT_EQ(server->LibraryInitialized(), 0);
  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
}

TEST(SherlockAudioStreamOutTest, ChangeRate96K) {
  static constexpr uint32_t kTestFrameRate1 = 48000;
  static constexpr uint32_t kTestFrameRate2 = 96000;
  static constexpr uint8_t kTestNumberOfChannels = 2;
  static constexpr uint32_t kTestFifoDepth = 16;
  struct CodecRate96KTest : Tas5720 {
    CodecRate96KTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
    zx_status_t Init(std::optional<uint8_t> slot, uint32_t rate) override {
      last_rate_requested_ = rate;
      return ZX_OK;
    }
    zx_status_t SetGain(float gain) override { return ZX_OK; }
    uint32_t last_rate_requested_ = 0;
  };
  struct Rate96KTest : public SherlockAudioStreamOut {
    Rate96KTest(zx_device_t* parent, fbl::Array<std::unique_ptr<Tas5720>> codecs,
                const gpio_protocol_t* audio_enable_gpio)
        : SherlockAudioStreamOut(parent) {
      codecs_ = std::move(codecs);
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

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  CodecRate96KTest* raw_codecs[3];
  raw_codecs[0] = new CodecRate96KTest(mock_i2c.GetProto());
  raw_codecs[1] = new CodecRate96KTest(mock_i2c.GetProto());
  raw_codecs[2] = new CodecRate96KTest(mock_i2c.GetProto());
  codecs[0] = std::unique_ptr<CodecRate96KTest>(raw_codecs[0]);
  codecs[1] = std::unique_ptr<CodecRate96KTest>(raw_codecs[1]);
  codecs[2] = std::unique_ptr<CodecRate96KTest>(raw_codecs[2]);
  auto server = audio::SimpleAudioStream::Create<Rate96KTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());
  ASSERT_NOT_NULL(server);

  Device::SyncClient client(std::move(tester.FidlClient()));
  Device::ResultOf::GetChannel channel_wrap = client.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  // After we get the channel we use audio::utils serialization until we convert to FIDL.
  auto channel_client = audio::utils::AudioOutput::Create(1);
  channel_client->SetStreamChannel(std::move(channel_wrap->channel));

  audio_sample_format_t format = AUDIO_SAMPLE_FORMAT_16BIT;
  ASSERT_OK(channel_client->SetFormat(kTestFrameRate2, kTestNumberOfChannels, format));
  ASSERT_EQ(raw_codecs[0]->last_rate_requested_, kTestFrameRate2);
  ASSERT_EQ(raw_codecs[1]->last_rate_requested_, kTestFrameRate2);
  ASSERT_EQ(raw_codecs[2]->last_rate_requested_, kTestFrameRate2);

  server->DdkUnbindDeprecated();
  EXPECT_TRUE(tester.Ok());
}

}  // namespace sherlock
}  // namespace audio
