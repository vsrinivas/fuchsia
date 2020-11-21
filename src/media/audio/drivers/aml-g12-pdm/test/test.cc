// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/sync/completion.h>

#include <ddktl/protocol/composite.h>
#include <fake-mmio-reg/fake-mmio-reg.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <mock/ddktl/protocol/gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zxtest/zxtest.h>

#include "../audio-stream-in.h"

namespace audio::aml_g12 {

namespace audio_fidl = ::llcpp::fuchsia::hardware::audio;

class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() : proto_({&pdev_protocol_ops_, this}) {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegCount);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t), kRegCount);
  }

  const pdev_protocol_t* proto() const { return &proto_; }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    EXPECT_LE(index, 1);
    out_mmio->offset = reinterpret_cast<size_t>(this);
    return ZX_OK;
  }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) {
    return fake_bti_create(out_bti->reset_and_get_address());
  }
  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }
  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  ddk::MmioBuffer mmio() { return ddk::MmioBuffer(mmio_->GetMmioBuffer()); }
  ddk_fake::FakeMmioReg& reg(size_t ix) {
    return regs_[ix >> 2];  // AML registers are in virtual address units.
  }

 private:
  static constexpr size_t kRegCount =
      S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  pdev_protocol_t proto_;
  std::unique_ptr<ddk_fake::FakeMmioReg[]> regs_;
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> mmio_;
};

metadata::AmlPdmConfig GetDefaultMetadata() {
  metadata::AmlPdmConfig metadata = {};
  snprintf(metadata.manufacturer, sizeof(metadata.manufacturer), "Test");
  snprintf(metadata.product_name, sizeof(metadata.product_name), "Test");
  metadata.number_of_channels = 2;
  metadata.version = metadata::AmlVersion::kS905D3G;
  metadata.sysClockDivFactor = 4;
  metadata.dClockDivFactor = 250;
  return metadata;
}

class TestAudioStreamIn : public AudioStreamIn {
 public:
  explicit TestAudioStreamIn() : AudioStreamIn(fake_ddk::kFakeParent) {}
  bool AllowNonContiguousRingBuffer() override { return true; }
};

audio_fidl::PcmFormat GetDefaultPcmFormat() {
  audio_fidl::PcmFormat format;
  format.number_of_channels = 2;
  format.channels_to_use_bitmask = 0x03;
  format.sample_format = audio_fidl::SampleFormat::PCM_SIGNED;
  format.frame_rate = 48'000;
  format.bytes_per_sample = 2;
  format.valid_bits_per_sample = 16;
  return format;
}

struct AudioStreamInTest : public zxtest::Test {
  void SetUp() override {
    static constexpr size_t kNumBindProtocols = 1;
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[kNumBindProtocols],
                                                  kNumBindProtocols);
    protocols[0] = {ZX_PROTOCOL_PDEV, *reinterpret_cast<const fake_ddk::Protocol*>(pdev_.proto())};
    tester_.SetProtocols(std::move(protocols));
  }
  void TestMasks(uint8_t number_of_channels, uint64_t channels_to_use_bitmask,
                 uint8_t channels_mask, uint8_t mute_mask) {
    auto metadata = GetDefaultMetadata();
    metadata.number_of_channels = number_of_channels;
    tester_.SetMetadata(&metadata, sizeof(metadata));

    int step = 0;  // Track of the expected sequence of reads and writes.
    pdev_.reg(0x000).SetReadCallback([]() -> uint32_t { return 0; });
    pdev_.reg(0x000).SetWriteCallback([&step, &mute_mask](size_t value) {
      if (step == 8) {
        EXPECT_EQ(mute_mask << 20, value);
      }
      step++;
    });

    auto server = audio::SimpleAudioStream::Create<TestAudioStreamIn>();
    ASSERT_NOT_NULL(server);

    audio_fidl::Device::SyncClient client_wrap(std::move(tester_.FidlClient()));
    audio_fidl::Device::ResultOf::GetChannel channel_wrap = client_wrap.GetChannel();
    ASSERT_EQ(channel_wrap.status(), ZX_OK);

    audio_fidl::StreamConfig::SyncClient client(std::move(channel_wrap->channel));

    audio_fidl::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = channels_to_use_bitmask;
    pcm_format.number_of_channels = number_of_channels;
    fidl::aligned<audio_fidl::PcmFormat> aligned_pcm_format = std::move(pcm_format);
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&aligned_pcm_format));
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    client.CreateRingBuffer(builder.build(), std::move(remote));
    // To make sure we have initialized in the server make a sync call
    // (we know the server is single threaded, initialization is completed if received a reply).
    auto props = audio_fidl::RingBuffer::Call::GetProperties(zx::unowned_channel(local));
    ASSERT_OK(props.status());

    server->DdkAsyncRemove();
    EXPECT_TRUE(tester_.Ok());
    server->DdkRelease();
    EXPECT_EQ(step, 12);
  }

  void TestRingBufferSize(uint8_t number_of_channels, uint32_t frames_req,
                          uint32_t frames_expected) {
    auto metadata = GetDefaultMetadata();
    metadata.number_of_channels = number_of_channels;
    tester_.SetMetadata(&metadata, sizeof(metadata));

    auto stream = audio::SimpleAudioStream::Create<TestAudioStreamIn>();
    audio_fidl::Device::SyncClient client_wrap(std::move(tester_.FidlClient()));
    audio_fidl::Device::ResultOf::GetChannel ch = client_wrap.GetChannel();
    ASSERT_EQ(ch.status(), ZX_OK);
    audio_fidl::StreamConfig::SyncClient client(std::move(ch->channel));
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    fidl::aligned<audio_fidl::PcmFormat> pcm_format = GetDefaultPcmFormat();
    pcm_format.value.number_of_channels = number_of_channels;
    auto builder = audio_fidl::Format::UnownedBuilder();
    builder.set_pcm_format(fidl::unowned_ptr(&pcm_format));
    client.CreateRingBuffer(builder.build(), std::move(remote));

    auto vmo = audio_fidl::RingBuffer::Call::GetVmo(zx::unowned_channel(local), frames_req, 0);
    ASSERT_OK(vmo.status());
    ASSERT_EQ(vmo.Unwrap()->result.response().num_frames, frames_expected);

    stream->DdkAsyncRemove();
    EXPECT_TRUE(tester_.Ok());
    stream->DdkRelease();
  }
  FakePDev pdev_;
  fake_ddk::Bind tester_;
};

TEST_F(AudioStreamInTest, ChannelsToUseBitmaskAllOn) {
  TestMasks(/*channels*/ 2, /*channels_to_use_bitmask*/ 3, /*channels_mask*/ 3, /*mute_mask*/ 0);
}
TEST_F(AudioStreamInTest, ChannelsToUseBitmaskLeftOn) {
  TestMasks(/*channels*/ 2, /*channels_to_use_bitmask*/ 1, /*channels_mask*/ 3, /*mute_mask*/ 2);
}
TEST_F(AudioStreamInTest, ChannelsToUseBitmaskRightOn) {
  TestMasks(/*channels*/ 2, /*channels_to_use_bitmask*/ 2, /*channels_mask*/ 3, /*mute_mask*/ 1);
}
TEST_F(AudioStreamInTest, ChannelsToUseBitmaskMoreThanNeeded) {
  TestMasks(/*channels*/ 2, /*channels_to_use_bitmask*/ 0xff, /*channels_mask*/ 3, /*mute_mask*/ 0);
}

// With 16 bits samples, frame size is 2 x number of channels bytes.
// Frames returned are rounded to HW buffer alignment (8 bytes) and frame size.
TEST_F(AudioStreamInTest, RingBufferSize1) {
  TestRingBufferSize(2, 1, 2);
}  // Rounded to HW buffer.
TEST_F(AudioStreamInTest, RingBufferSize2) {
  TestRingBufferSize(2, 3, 4);
}  // Rounded to HW buffer.
TEST_F(AudioStreamInTest, RingBufferSize3) { TestRingBufferSize(3, 1, 4); }  // Rounded to both.
TEST_F(AudioStreamInTest, RingBufferSize4) { TestRingBufferSize(3, 3, 4); }  // Rounded to both.
TEST_F(AudioStreamInTest, RingBufferSize5) {
  TestRingBufferSize(8, 1, 1);
}  // Rounded to frame size.
TEST_F(AudioStreamInTest, RingBufferSize6) {
  TestRingBufferSize(8, 3, 3);
}  // Rounded to frame size.
}  // namespace audio::aml_g12

// Redefine PDevMakeMmioBufferWeak per the recommendation in pdev.h.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* test_harness = reinterpret_cast<audio::aml_g12::FakePDev*>(pdev_mmio.offset);
  mmio->emplace(test_harness->mmio());
  return ZX_OK;
}
