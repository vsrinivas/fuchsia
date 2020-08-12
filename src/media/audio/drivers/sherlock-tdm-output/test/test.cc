// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/audio/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/mock-i2c/mock-i2c.h>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/gpio.h>
#include <zxtest/zxtest.h>

#include "../audio-stream-out.h"

namespace audio {
namespace sherlock {

static constexpr uint32_t kTestFrameRate1 = 48'000;
static constexpr uint32_t kTestFrameRate2 = 96'000;
static constexpr uint8_t kTestNumberOfChannels = 4;
static constexpr uint8_t kTestChannelsToUseBitmask = 0xf;
static constexpr uint32_t kTestFifoDepth = 16;
static constexpr size_t kMaxLanes = 2;

using ::llcpp::fuchsia::hardware::audio::Device;
namespace audio_fidl = ::llcpp::fuchsia::hardware::audio;

audio_fidl::PcmFormat GetDefaultPcmFormat() {
  audio_fidl::PcmFormat format;
  format.number_of_channels = kTestNumberOfChannels;
  format.channels_to_use_bitmask = kTestChannelsToUseBitmask;
  format.sample_format = audio_fidl::SampleFormat::PCM_SIGNED;
  format.frame_rate = kTestFrameRate1;
  format.bytes_per_sample = 2;
  format.valid_bits_per_sample = 16;
  return format;
}

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

struct Tas5720GainTest : Tas5720 {
  Tas5720GainTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
  zx_status_t Init(std::optional<uint8_t> slot, uint32_t rate) override { return ZX_OK; }
};

struct AmlTdmDeviceTest : public AmlTdmOutDevice {
  template <typename T>
  static std::unique_ptr<T> Create() {
    constexpr size_t n_registers = 4096;  // big enough.
    static fbl::Array<ddk_mock::MockMmioReg> unused_mocks =
        fbl::Array(new ddk_mock::MockMmioReg[n_registers], n_registers);
    static ddk_mock::MockMmioRegRegion unused_region(unused_mocks.data(), sizeof(uint32_t),
                                                     n_registers);
    return std::make_unique<T>(unused_region.GetMmioBuffer(), HIFI_PLL, TDM_OUT_C, FRDDR_A, MCLK_C,
                               0, metadata::AmlVersion::kS905D2G);
  }
  AmlTdmDeviceTest(ddk::MmioBuffer mmio, ee_audio_mclk_src_t clk_src, aml_tdm_out_t tdm,
                   aml_frddr_t frddr, aml_tdm_mclk_t mclk, uint32_t fifo_depth,
                   metadata::AmlVersion version)
      : AmlTdmOutDevice(std::move(mmio), clk_src, tdm, frddr, mclk, fifo_depth, version) {}
  void Initialize() override { initialize_called_++; }
  void Shutdown() override { shutdown_called_++; }
  size_t initialize_called_ = 0;
  size_t shutdown_called_ = 0;
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

struct SherlockAudioStreamOutDefaultTest : public SherlockAudioStreamOut {
  SherlockAudioStreamOutDefaultTest(zx_device_t* parent,
                                    fbl::Array<std::unique_ptr<Tas5720>> codecs,
                                    const gpio_protocol_t* audio_enable_gpio)
      : SherlockAudioStreamOut(parent) {
    codecs_ = std::move(codecs);
    audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
    aml_audio_ = AmlTdmDeviceTest::Create<AmlTdmDeviceTest>();
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

TEST(SherlockAudioStreamOutTest, MuteChannels) {
  struct AmlTdmDeviceMuteTest : public AmlTdmDeviceTest {
    AmlTdmDeviceMuteTest(ddk::MmioBuffer mmio, ee_audio_mclk_src_t clk_src, aml_tdm_out_t tdm,
                         aml_frddr_t frddr, aml_tdm_mclk_t mclk, uint32_t fifo_depth,
                         metadata::AmlVersion version)
        : AmlTdmDeviceTest(std::move(mmio), clk_src, tdm, frddr, mclk, fifo_depth, version) {}
    zx_status_t ConfigTdmLane(size_t lane, uint32_t enable_mask, uint32_t mute_mask) override {
      if (lane >= kMaxLanes) {
        return ZX_ERR_INTERNAL;
      }
      last_enable_mask_[lane] = enable_mask;
      last_mute_mask_[lane] = mute_mask;
      return ZX_OK;
    }
    virtual ~AmlTdmDeviceMuteTest() = default;

    uint32_t last_enable_mask_[kMaxLanes] = {};
    uint32_t last_mute_mask_[kMaxLanes] = {};
  };
  struct SherlockAudioStreamOutMuteTest : public SherlockAudioStreamOutDefaultTest {
    SherlockAudioStreamOutMuteTest(zx_device_t* parent, fbl::Array<std::unique_ptr<Tas5720>> codecs,
                                   const gpio_protocol_t* audio_enable_gpio)
        : SherlockAudioStreamOutDefaultTest(parent, std::move(codecs), audio_enable_gpio) {
      aml_audio_ = AmlTdmDeviceMuteTest::Create<AmlTdmDeviceMuteTest>();
    }
    AmlTdmDevice* GetAmlTdmDevice() { return aml_audio_.get(); }
  };

  fake_ddk::Bind tester;
  mock_i2c::MockI2c mock_i2c;

  ddk::MockGpio audio_enable_gpio;
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 1);
  audio_enable_gpio.ExpectWrite(ZX_OK, 0);

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[1] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  codecs[2] = std::make_unique<Tas5720GoodInitTest>(mock_i2c.GetProto());
  auto server = audio::SimpleAudioStream::Create<SherlockAudioStreamOutMuteTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());
  ASSERT_NOT_NULL(server);

  Device::SyncClient client_wrap(std::move(tester.FidlClient()));
  Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

  auto aml = static_cast<AmlTdmDeviceMuteTest*>(server->GetAmlTdmDevice());

  // 1st case everything enabled.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the server, make a sync call
    // (we know the server is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // All 4 channels enabled, nothing muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 3);
  EXPECT_EQ(aml->last_mute_mask_[0], 0);
  EXPECT_EQ(aml->last_enable_mask_[1], 3);
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  // 2nd case only 1 channel enabled.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = 1;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the server, make a sync call
    // (we know the server is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // All 4 channels enabled, 3 muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 3);
  EXPECT_EQ(aml->last_mute_mask_[0], 2);  // Mutes 1 channel in lane 0.
  EXPECT_EQ(aml->last_enable_mask_[1], 3);
  EXPECT_EQ(aml->last_mute_mask_[1], 3);  // Mutes 2 channels in lane 1.

  // 3rd case 2 channels enabled.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = 0xa;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the server, make a sync call
    // (we know the server is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // All 4 channels enabled, 2 muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 3);
  EXPECT_EQ(aml->last_mute_mask_[0], 1);  // Mutes 1 channel in lane 0.
  EXPECT_EQ(aml->last_enable_mask_[1], 3);
  EXPECT_EQ(aml->last_mute_mask_[1], 1);  // Mutes 1 channel in lane 1.

  // 4th case all channels enabled when channels_to_use_bitmask is 0.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = 0;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure call initialization in the server, make a sync call
    // (we know the server is single threaded, init completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());
  }
  // All 4 channels enabled, nothing muted.
  EXPECT_EQ(aml->last_enable_mask_[0], 3);
  EXPECT_EQ(aml->last_mute_mask_[0], 0);
  EXPECT_EQ(aml->last_enable_mask_[1], 3);
  EXPECT_EQ(aml->last_mute_mask_[1], 0);

  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
  server->DdkRelease();
}

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
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
  server->DdkRelease();
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
      aml_audio_ = AmlTdmDeviceTest::Create<AmlTdmDeviceTest>();
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
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
  server->DdkRelease();
}

TEST(SherlockAudioStreamOutTest, LibraryShutdwonOnInitWithError) {
  struct LibInitTest : public SherlockAudioStreamOut {
    LibInitTest(zx_device_t* parent, fbl::Array<std::unique_ptr<Tas5720>> codecs,
                const gpio_protocol_t* audio_enable_gpio)
        : SherlockAudioStreamOut(parent) {
      codecs_ = std::move(codecs);
      audio_en_ = ddk::GpioProtocolClient(audio_enable_gpio);
      aml_audio_ = AmlTdmDeviceTest::Create<AmlTdmDeviceTest>();
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
  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  audio_enable_gpio.VerifyAndClear();
  server->DdkRelease();
}

TEST(SherlockAudioStreamOutTest, ChangeRate96K) {
  struct CodecRate96KTest : Tas5720 {
    CodecRate96KTest(ddk::I2cChannel i2c) : Tas5720(std::move(i2c)) {}
    zx_status_t Init(std::optional<uint8_t> slot, uint32_t rate) override {
      last_rate_requested_ = rate;
      return ZX_OK;
    }
    zx_status_t SetGain(float gain) override { return ZX_OK; }
    uint32_t last_rate_requested_ = 0;
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
  auto server = audio::SimpleAudioStream::Create<SherlockAudioStreamOutDefaultTest>(
      fake_ddk::kFakeParent, std::move(codecs), audio_enable_gpio.GetProto());
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

  ASSERT_EQ(raw_codecs[0]->last_rate_requested_, kTestFrameRate2);
  ASSERT_EQ(raw_codecs[1]->last_rate_requested_, kTestFrameRate2);
  ASSERT_EQ(raw_codecs[2]->last_rate_requested_, kTestFrameRate2);

  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  server->DdkRelease();
}

TEST(SherlockAudioStreamOutTest, SetRate) {
  fake_ddk::Bind tester;

  zx::interrupt irq;
  zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq);

  mock_i2c::MockI2c mock_i2c0;
  mock_i2c::MockI2c mock_i2c1;
  mock_i2c::MockI2c mock_i2c2;

  constexpr float kDeltaGainWooferVsTweeters = 12.6f;

  // Default init, tweeters at analog gain 0, woofer at analog gain 3 (analog delta 7.1dB).
  uint8_t woofer = 0xcf;
  float tweeter_delta = 2.f * (kDeltaGainWooferVsTweeters - 7.1f);
  uint8_t tweeter = static_cast<uint8_t>(static_cast<float>(woofer) - tweeter_delta);
  mock_i2c0.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c1.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c2.ExpectWriteStop({0x06, 0x5d}).ExpectWriteStop({0x04, woofer});

  // At -2.8f gain, tweeters at analog gain 0, woofer at analog gain 2 (analog delta 4.3dB).
  tweeter_delta = 2.f * (kDeltaGainWooferVsTweeters - 4.3f);
  tweeter = static_cast<uint8_t>(static_cast<float>(woofer) - tweeter_delta);
  mock_i2c0.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c1.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c2.ExpectWriteStop({0x06, 0x59}).ExpectWriteStop({0x04, woofer});

  // At -5.6f gain, tweeters at analog gain 0, woofer at analog gain 1 (analog delta 1.5dB).
  tweeter_delta = 2.f * (kDeltaGainWooferVsTweeters - 1.5f);
  tweeter = static_cast<uint8_t>(static_cast<float>(woofer) - tweeter_delta);
  mock_i2c0.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c1.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c2.ExpectWriteStop({0x06, 0x55}).ExpectWriteStop({0x04, woofer});

  // At -7.1f gain, tweeters at analog gain 0, woofer at analog gain 0 (analog delta 0dB).
  tweeter_delta = 2.f * kDeltaGainWooferVsTweeters;
  tweeter = static_cast<uint8_t>(static_cast<float>(woofer) - tweeter_delta);
  mock_i2c0.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c1.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c2.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, woofer});

  // At +1.2f gain, tweeters at analog gain 0, woofer at analog gain 3 (analog delta 7.1dB).
  woofer = 0xd1;
  tweeter_delta = 2.f * (kDeltaGainWooferVsTweeters - 7.1f);
  tweeter = static_cast<uint8_t>(static_cast<float>(woofer) - tweeter_delta);
  mock_i2c0.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c1.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, tweeter});
  mock_i2c2.ExpectWriteStop({0x06, 0x5d}).ExpectWriteStop({0x04, woofer});

  // Lowest allowed gain.
  mock_i2c0.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, 0x00});
  mock_i2c1.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, 0x00});
  mock_i2c2.ExpectWriteStop({0x06, 0x51}).ExpectWriteStop({0x04, 0x00});

  // Highest allowed gain.
  mock_i2c0.ExpectWriteStop({0x06, 0x5d}).ExpectWriteStop({0x04, 0xff});
  mock_i2c1.ExpectWriteStop({0x06, 0x5d}).ExpectWriteStop({0x04, 0xff});
  mock_i2c2.ExpectWriteStop({0x06, 0x5d}).ExpectWriteStop({0x04, 0xff});

  ddk::MockGpio mock_ena;
  mock_ena.ExpectWrite(ZX_OK, 1);

  auto codecs = fbl::Array(new std::unique_ptr<Tas5720>[3], 3);
  codecs[0] = std::make_unique<Tas5720GainTest>(mock_i2c0.GetProto());
  codecs[1] = std::make_unique<Tas5720GainTest>(mock_i2c1.GetProto());
  codecs[2] = std::make_unique<Tas5720GainTest>(mock_i2c2.GetProto());
  Tas5720* codecs_raw[3];  // Alive for the duration of the test.
  codecs_raw[0] = codecs[0].get();
  codecs_raw[1] = codecs[1].get();
  codecs_raw[2] = codecs[2].get();
  struct GainTest : public SherlockAudioStreamOutCodecInitTest {
    GainTest(zx_device_t* parent, fbl::Array<std::unique_ptr<Tas5720>> codecs,
             const gpio_protocol_t* audio_enable_gpio)
        : SherlockAudioStreamOutCodecInitTest(parent, std::move(codecs), audio_enable_gpio) {}
    zx_status_t SetGain(const audio_proto::SetGainReq& req) override {
      ScopedToken t(domain_token());
      return SherlockAudioStreamOutCodecInitTest::SetGain(req);
    }
  };
  auto server = audio::SimpleAudioStream::Create<GainTest>(fake_ddk::kFakeParent, std::move(codecs),
                                                           mock_ena.GetProto());

  ASSERT_NOT_NULL(server);

  ASSERT_EQ(codecs_raw[0]->GetGain(), -kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[1]->GetGain(), -kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[2]->GetGain(), 0.f);

  audio_proto::SetGainReq req = {};
  req.gain = -2.8f;
  server->SetGain(req);
  ASSERT_EQ(codecs_raw[0]->GetGain(), -2.8f - kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[1]->GetGain(), -2.8f - kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[2]->GetGain(), -2.8f);

  req.gain = -5.6f;
  server->SetGain(req);
  ASSERT_EQ(codecs_raw[0]->GetGain(), -5.6f - kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[1]->GetGain(), -5.6f - kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[2]->GetGain(), -5.6f);

  req.gain = -7.1f;
  server->SetGain(req);
  ASSERT_EQ(codecs_raw[0]->GetGain(), -7.1f - kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[1]->GetGain(), -7.1f - kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[2]->GetGain(), -7.1f);

  req.gain = 1.2f;
  server->SetGain(req);
  ASSERT_EQ(codecs_raw[0]->GetGain(), 1.2f - kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[1]->GetGain(), 1.2f - kDeltaGainWooferVsTweeters);
  ASSERT_EQ(codecs_raw[2]->GetGain(), 1.2f);

  req.gain = -200.f;
  server->SetGain(req);
  // Lowest allowed gain.
  ASSERT_EQ(codecs_raw[0]->GetGain(), -(103.5f + 7.1f));
  ASSERT_EQ(codecs_raw[1]->GetGain(), -(103.5f + 7.1f));
  ASSERT_EQ(codecs_raw[2]->GetGain(), -(103.5f + 7.1f));

  req.gain = 200.f;
  server->SetGain(req);
  // Highest allowed gain.
  ASSERT_EQ(codecs_raw[0]->GetGain(), 24.f);
  ASSERT_EQ(codecs_raw[1]->GetGain(), 24.f);
  ASSERT_EQ(codecs_raw[2]->GetGain(), 24.f);

  server->DdkAsyncRemove();
  EXPECT_TRUE(tester.Ok());
  mock_ena.VerifyAndClear();
  mock_i2c0.VerifyAndClear();
  mock_i2c1.VerifyAndClear();
  mock_i2c2.VerifyAndClear();
  server->DdkRelease();
}

}  // namespace sherlock
}  // namespace audio
