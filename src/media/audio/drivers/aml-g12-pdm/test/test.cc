// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/ddk/metadata.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/sync/completion.h>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zxtest/zxtest.h>

#include "../audio-stream-in.h"
#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"

namespace audio::aml_g12 {

namespace audio_fidl = fuchsia_hardware_audio;

class FakeMmio {
 public:
  FakeMmio() {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegCount);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t), kRegCount);
  }

  fake_pdev::FakePDev::MmioInfo mmio_info() { return {.offset = reinterpret_cast<size_t>(this)}; }

  ddk::MmioBuffer mmio() { return ddk::MmioBuffer(mmio_->GetMmioBuffer()); }
  ddk_fake::FakeMmioReg& reg(size_t ix) {
    return regs_[ix >> 2];  // AML registers are in virtual address units.
  }

 private:
  static constexpr size_t kRegCount =
      S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
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
  inspect::Inspector& inspect() { return AudioStreamIn::inspect(); }
};

audio_fidl::wire::PcmFormat GetDefaultPcmFormat() {
  audio_fidl::wire::PcmFormat format;
  format.number_of_channels = 2;
  format.channels_to_use_bitmask = 0x03;
  format.sample_format = audio_fidl::wire::SampleFormat::kPcmSigned;
  format.frame_rate = 48'000;
  format.bytes_per_sample = 2;
  format.valid_bits_per_sample = 16;
  return format;
}

struct AudioStreamInTest : public inspect::InspectTestHelper, public zxtest::Test {
  void SetUp() override {
    pdev_.set_mmio(0, mmio_.mmio_info());
    pdev_.set_mmio(1, mmio_.mmio_info());
    pdev_.UseFakeBti();
    zx::interrupt irq;
    ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));
    pdev_.set_interrupt(0, std::move(irq));

    tester_.SetProtocol(ZX_PROTOCOL_PDEV, pdev_.proto());
  }

  void TestMasks(uint8_t number_of_channels, uint64_t channels_to_use_bitmask,
                 uint8_t channels_mask, uint8_t mute_mask) {
    auto metadata = GetDefaultMetadata();
    metadata.number_of_channels = number_of_channels;
    tester_.SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

    int step = 0;  // Track of the expected sequence of reads and writes.
    mmio_.reg(0x000).SetReadCallback([]() -> uint32_t { return 0; });
    mmio_.reg(0x000).SetWriteCallback([&step, &mute_mask](size_t value) {
      if (step == 8) {
        EXPECT_EQ(mute_mask << 20, value);
      }
      step++;
    });

    auto server = audio::SimpleAudioStream::Create<TestAudioStreamIn>();
    ASSERT_NOT_NULL(server);

    fidl::WireSyncClient<audio_fidl::Device> client_wrap(tester_.FidlClient<audio_fidl::Device>());
    fidl::WireResult<audio_fidl::Device::GetChannel> channel_wrap = client_wrap.GetChannel();
    ASSERT_EQ(channel_wrap.status(), ZX_OK);

    fidl::WireSyncClient<audio_fidl::StreamConfig> client(std::move(channel_wrap->channel));

    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.channels_to_use_bitmask = channels_to_use_bitmask;
    pcm_format.number_of_channels = number_of_channels;

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    format.set_pcm_format(allocator, std::move(pcm_format));

    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    client.CreateRingBuffer(std::move(format), std::move(remote));

    // To make sure we have initialized in the server make a sync call
    // (we know the server is single threaded, initialization is completed if received a reply).
    auto props = fidl::WireCall<audio_fidl::RingBuffer>(local).GetProperties();
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
    tester_.SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

    auto stream = audio::SimpleAudioStream::Create<TestAudioStreamIn>();
    auto client_wrap = fidl::BindSyncClient(tester_.FidlClient<audio_fidl::Device>());
    fidl::WireResult<audio_fidl::Device::GetChannel> ch = client_wrap.GetChannel();
    ASSERT_EQ(ch.status(), ZX_OK);
    fidl::WireSyncClient<audio_fidl::StreamConfig> client(std::move(ch->channel));
    auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = *std::move(endpoints);

    audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();
    pcm_format.number_of_channels = number_of_channels;

    fidl::Arena allocator;
    audio_fidl::wire::Format format(allocator);
    format.set_pcm_format(allocator, std::move(pcm_format));

    client.CreateRingBuffer(std::move(format), std::move(remote));

    auto vmo = fidl::WireCall<audio_fidl::RingBuffer>(local).GetVmo(frames_req, 0);
    ASSERT_OK(vmo.status());
    ASSERT_EQ(vmo.Unwrap()->result.response().num_frames, frames_expected);

    stream->DdkAsyncRemove();
    EXPECT_TRUE(tester_.Ok());
    stream->DdkRelease();
  }
  fake_pdev::FakePDev pdev_;
  FakeMmio mmio_;
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

TEST_F(AudioStreamInTest, Inspect) {
  auto metadata = GetDefaultMetadata();
  tester_.SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  auto server = audio::SimpleAudioStream::Create<TestAudioStreamIn>();
  ASSERT_NOT_NULL(server);

  fidl::WireSyncClient<audio_fidl::Device> client_wrap(tester_.FidlClient<audio_fidl::Device>());
  fidl::WireResult<audio_fidl::Device::GetChannel> channel_wrap = client_wrap.GetChannel();
  ASSERT_EQ(channel_wrap.status(), ZX_OK);

  fidl::WireSyncClient<audio_fidl::StreamConfig> client(std::move(channel_wrap->channel));

  audio_fidl::wire::PcmFormat pcm_format = GetDefaultPcmFormat();

  fidl::Arena allocator;
  audio_fidl::wire::Format format(allocator);
  format.set_pcm_format(allocator, std::move(pcm_format));

  auto endpoints = fidl::CreateEndpoints<audio_fidl::RingBuffer>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = *std::move(endpoints);

  client.CreateRingBuffer(std::move(format), std::move(remote));

  auto props = fidl::WireCall<audio_fidl::RingBuffer>(local).GetProperties();
  ASSERT_OK(props.status());

  // Check inspect state.
  ASSERT_NO_FATAL_FAILURES(ReadInspect(server->inspect().DuplicateVmo()));
  auto* simple_audio = hierarchy().GetByPath({"simple_audio_stream"});
  ASSERT_TRUE(simple_audio);
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(simple_audio->node(), "state", inspect::StringPropertyValue("created")));
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(hierarchy().node(), "status_time", inspect::IntPropertyValue(0)));
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(hierarchy().node(), "dma_status", inspect::UintPropertyValue(0)));
  ASSERT_NO_FATAL_FAILURES(
      CheckProperty(hierarchy().node(), "pdm_status", inspect::UintPropertyValue(0)));

  server->DdkAsyncRemove();
  EXPECT_TRUE(tester_.Ok());
  server->DdkRelease();
}

}  // namespace audio::aml_g12

// Redefine PDevMakeMmioBufferWeak per the recommendation in pdev.h.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* test_harness = reinterpret_cast<audio::aml_g12::FakeMmio*>(pdev_mmio.offset);
  mmio->emplace(test_harness->mmio());
  return ZX_OK;
}
