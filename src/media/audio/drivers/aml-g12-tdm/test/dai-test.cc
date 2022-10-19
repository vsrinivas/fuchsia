// Copyright 2020 The Fuchsia Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../dai.h"

#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/ddk/metadata.h>
#include <lib/device-protocol/pdev.h>
#include <lib/sync/completion.h>

#include <thread>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <soc/aml-common/aml-audio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zxtest/zxtest.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace audio::aml_g12 {

fuchsia_hardware_audio::wire::PcmFormat GetDefaultPcmFormat() {
  fuchsia_hardware_audio::wire::PcmFormat format;
  format.number_of_channels = 2;
  format.sample_format = fuchsia_hardware_audio::wire::SampleFormat::kPcmSigned;
  format.frame_rate = 48'000;
  format.bytes_per_sample = 2;
  format.valid_bits_per_sample = 16;
  return format;
}

fuchsia_hardware_audio::wire::DaiFormat GetDefaultDaiFormat() {
  fuchsia_hardware_audio::wire::DaiFormat format;
  format.number_of_channels = 2;
  format.sample_format = fuchsia_hardware_audio::wire::DaiSampleFormat::kPcmSigned;
  format.frame_format = fuchsia_hardware_audio::wire::DaiFrameFormat::WithFrameFormatStandard(
      fuchsia_hardware_audio::wire::DaiFrameFormatStandard::kI2S);
  format.frame_rate = 48'000;
  format.bits_per_slot = 16;
  format.bits_per_sample = 32;
  return format;
}

metadata::AmlConfig GetDefaultMetadata() {
  metadata::AmlConfig metadata = {};
  metadata.is_input = false;
  metadata.mClockDivFactor = 10;
  metadata.sClockDivFactor = 25;
  metadata.ring_buffer.number_of_channels = 2;
  metadata.lanes_enable_mask[0] = 3;
  metadata.bus = metadata::AmlBus::TDM_C;
  metadata.version = metadata::AmlVersion::kS905D2G;
  metadata.dai.type = metadata::DaiType::I2s;
  metadata.dai.number_of_channels = 2;
  metadata.dai.bits_per_sample = 16;
  metadata.dai.bits_per_slot = 32;
  return metadata;
}

struct DaiClient {
  DaiClient(ddk::DaiProtocolClient proto_client) {
    proto_client_ = proto_client;
    ZX_ASSERT(proto_client_.is_valid());
    zx::channel channel_remote, channel_local;
    ASSERT_OK(zx::channel::create(0, &channel_local, &channel_remote));
    ASSERT_OK(proto_client_.Connect(std::move(channel_remote)));
    dai_.Bind(std::move(channel_local));
  }
  ddk::DaiProtocolClient proto_client_;
  ::fuchsia::hardware::audio::DaiSyncPtr dai_;
};

class FakeMmio {
 public:
  FakeMmio() {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegCount);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t), kRegCount);
  }

  fake_pdev::FakePDev::MmioInfo mmio_info() { return {.offset = reinterpret_cast<size_t>(this)}; }

  fdf::MmioBuffer mmio() { return fdf::MmioBuffer(mmio_->GetMmioBuffer()); }
  ddk_fake::FakeMmioReg& reg(size_t ix) {
    return regs_[ix >> 2];  // AML registers are in virtual address units.
  }

 private:
  static constexpr size_t kRegCount =
      S905D2_EE_AUDIO_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
  std::unique_ptr<ddk_fake::FakeMmioReg[]> regs_;
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> mmio_;
};

class TestAmlG12TdmDai : public AmlG12TdmDai {
 public:
  explicit TestAmlG12TdmDai(zx_device_t* parent, ddk::PDev pdev)
      : AmlG12TdmDai(parent, std::move(pdev)) {}
  dai_protocol_t GetProto() { return {&this->dai_protocol_ops_, this}; }
  bool AllowNonContiguousRingBuffer() override { return true; }
  void Stop(StopCallback callback) override {
    AmlG12TdmDai::Stop(std::move(callback));
    sync_completion_signal(&stopped_);
  }
  void WaitUntilStopped() { sync_completion_wait(&stopped_, ZX_TIME_INFINITE); }

 private:
  sync_completion_t stopped_ = {};
};

class AmlG12TdmDaiTest : public zxtest::Test {
 public:
  void SetUp() override {
    pdev_.set_mmio(0, mmio_.mmio_info());
    pdev_.UseFakeBti();
  }

 protected:
  FakeMmio mmio_;
  fake_pdev::FakePDev pdev_;
};

TEST_F(AmlG12TdmDaiTest, InitializeI2sOut) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::AmlConfig metadata = {};
  metadata.is_input = false;
  metadata.mClockDivFactor = 10;
  metadata.sClockDivFactor = 25;
  metadata.ring_buffer.number_of_channels = 2;
  metadata.lanes_enable_mask[0] = 3;
  metadata.bus = metadata::AmlBus::TDM_C;
  metadata.version = metadata::AmlVersion::kS905D2G;
  metadata.dai.type = metadata::DaiType::I2s;
  metadata.dai.number_of_channels = 2;
  metadata.dai.bits_per_sample = 16;
  metadata.dai.bits_per_slot = 32;
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  auto dai = std::make_unique<TestAmlG12TdmDai>(fake_parent.get(), pdev_.proto());
  auto dai_proto = dai->GetProto();
  ASSERT_OK(dai->InitPDev());
  ASSERT_OK(dai->DdkAdd("test"));
  dai.release();  // Managed by the DDK.
  // step helps track of the expected sequence of reads and writes.
  int step = 0;

  // Configure TDM OUT for I2S.
  // TDM OUT CTRL0 disable, then
  // TDM OUT CTRL0 config, bitoffset 2, 2 slots, 32 bits per slot.
  mmio_.reg(0x580).SetReadCallback([&step]() -> uint32_t {
    if (step == 0) {
      return 0xffff'ffff;
    } else if (step == 3) {
      return 0x0000'0000;
    } else if (step == 6) {
      return 0x3001'003f;
    } else if (step == 7) {
      return 0x0001'003f;
    } else if (step == 8) {
      return 0x2001'003f;
    } else if (step == 9) {
      return 0x8001'003f;
    }
    ADD_FAILURE();
    return 0;
  });
  mmio_.reg(0x580).SetWriteCallback([&step](size_t value) {
    if (step == 0) {
      EXPECT_EQ(0x7fff'ffff, value);  // Disable.
      step++;
    } else if (step == 3) {
      EXPECT_EQ(0x0001'003f, value);
      step++;
    } else if (step == 6) {
      EXPECT_EQ(0x0001'003f, value);  // Sync.
      step++;
    } else if (step == 7) {
      EXPECT_EQ(0x2001'003f, value);  // Sync.
      step++;
    } else if (step == 8) {
      EXPECT_EQ(0x3001'003f, value);  // Sync.
      step++;
    } else if (step == 9) {
      EXPECT_EQ(0x0001'003f, value);  // Disable on Shutdown.
      step++;
    } else {
      EXPECT_TRUE(0);
    }
  });

  // TDM OUT CTRL1 FRDDR C with 16 bits per sample.
  mmio_.reg(0x584).SetWriteCallback([](size_t value) { EXPECT_EQ(0x0200'0f20, value); });

  // SCLK CTRL, enabled, 24 sdiv, 31 lrduty, 63 lrdiv.
  mmio_.reg(0x050).SetWriteCallback([](size_t value) { EXPECT_EQ(0xc180'7c3f, value); });

  // SCLK CTRL1, clear delay, sclk_invert_ph0.
  mmio_.reg(0x054).SetWriteCallback([&step](size_t value) {
    if (step == 4) {
      EXPECT_EQ(0x0000'0000, value);
      step++;
    } else if (step == 5) {
      EXPECT_EQ(0x0000'0001, value);
      step++;
    }
  });

  // CLK TDMOUT CTL, enable, no sclk_inv, sclk_ws_inv, mclk_ch 2.
  mmio_.reg(0x098).SetWriteCallback([&step](size_t value) {
    if (step == 1) {
      EXPECT_EQ(0x0000'0000, value);  // Disable
      step++;
    } else if (step == 2) {
      EXPECT_EQ(0xd220'0000, value);
      step++;
    } else if (step == 10) {
      EXPECT_EQ(0x0000'0000, value);  // Disable on Shutdown
      step++;
    }
  });

  DaiClient client(&dai_proto);
  client.dai_->Reset();
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  child_dev->ReleaseOp();
  EXPECT_EQ(step, 11);
}

TEST_F(AmlG12TdmDaiTest, InitializePcmOut) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::AmlConfig metadata = GetDefaultMetadata();
  metadata.ring_buffer.number_of_channels = 1;
  metadata.lanes_enable_mask[0] = 1;
  metadata.dai.type = metadata::DaiType::Tdm1;
  metadata.dai.number_of_channels = 1;
  metadata.dai.bits_per_sample = 16;
  metadata.dai.bits_per_slot = 16;
  metadata.dai.sclk_on_raising = true;
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  auto dai = std::make_unique<TestAmlG12TdmDai>(fake_parent.get(), pdev_.proto());
  auto dai_proto = dai->GetProto();
  ASSERT_OK(dai->InitPDev());
  ASSERT_OK(dai->DdkAdd("test"));
  dai.release();  // Managed by the DDK.

  // step helps track of the expected sequence of reads and writes.
  int step = 0;

  // Configure TDM OUT for PCM.
  // TDM OUT CTRL0 disable, then
  // TDM OUT CTRL0 config, bitoffset 2, 1 slot, 16 bits per slot.
  mmio_.reg(0x580).SetReadCallback([&step]() -> uint32_t {
    if (step == 0) {
      return 0xffff'ffff;
    } else if (step == 3) {
      return 0x0000'0000;
    } else if (step == 6) {
      return 0x3001'000f;
    } else if (step == 7) {
      return 0x0001'000f;
    } else if (step == 8) {
      return 0x2001'000f;
    } else if (step == 9) {
      return 0x8001'000f;
    }
    ADD_FAILURE();
    return 0;
  });
  mmio_.reg(0x580).SetWriteCallback([&step](size_t value) {
    if (step == 0) {
      EXPECT_EQ(0x7fff'ffff, value);  // Disable.
      step++;
    } else if (step == 3) {
      EXPECT_EQ(0x0001'000f, value);
      step++;
    } else if (step == 6) {
      EXPECT_EQ(0x0001'000f, value);  // Sync.
      step++;
    } else if (step == 7) {
      EXPECT_EQ(0x2001'000f, value);  // Sync.
      step++;
    } else if (step == 8) {
      EXPECT_EQ(0x3001'000f, value);  // Sync.
      step++;
    } else if (step == 9) {
      EXPECT_EQ(0x0001'000f, value);  // Disable on Shutdown.
      step++;
    } else {
      EXPECT_TRUE(0);
    }
  });

  // TDM OUT CTRL1 FRDDR C with 16 bits per sample.
  mmio_.reg(0x584).SetWriteCallback([](size_t value) { EXPECT_EQ(0x0200'0f20, value); });

  // SCLK CTRL, enabled, 24 sdiv, 0 lrduty, 15 lrdiv.
  mmio_.reg(0x050).SetWriteCallback([](size_t value) { EXPECT_EQ(0xc180'000f, value); });

  // SCLK CTRL1, clear delay, no sclk_invert_ph0.
  mmio_.reg(0x054).SetWriteCallback([&step](size_t value) {
    if (step == 4) {
      EXPECT_EQ(0x0000'0000, value);
      step++;
    } else if (step == 5) {
      EXPECT_EQ(0x0000'0000, value);
      step++;
    }
  });

  // CLK TDMOUT CTL, enable, no sclk_inv, sclk_ws_inv, mclk_ch 2.
  mmio_.reg(0x098).SetWriteCallback([&step](size_t value) {
    if (step == 1) {
      EXPECT_EQ(0x0000'0000, value);  // Disable
      step++;
    } else if (step == 2) {
      EXPECT_EQ(0xd220'0000, value);
      step++;
    } else if (step == 10) {
      EXPECT_EQ(0x0000'0000, value);  // Disable on Shutdown
      step++;
    }
  });

  DaiClient client(&dai_proto);
  client.dai_->Reset();
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  child_dev->ReleaseOp();
  EXPECT_EQ(step, 11);
}

TEST_F(AmlG12TdmDaiTest, GetPropertiesOutputDai) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::AmlConfig metadata = GetDefaultMetadata();
  const std::string kTestString("test");
  strncpy(metadata.manufacturer, kTestString.c_str(), sizeof(metadata.manufacturer));
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  auto dai = std::make_unique<TestAmlG12TdmDai>(fake_parent.get(), pdev_.proto());
  auto dai_proto = dai->GetProto();
  ASSERT_OK(dai->InitPDev());
  ASSERT_OK(dai->DdkAdd("test"));
  dai.release();  // Managed by the DDK.

  DaiClient client(&dai_proto);

  ::fuchsia::hardware::audio::DaiProperties properties_out;
  ASSERT_OK(client.dai_->GetProperties(&properties_out));
  ASSERT_FALSE(properties_out.is_input());
  ASSERT_TRUE(properties_out.manufacturer() == kTestString);
  ASSERT_TRUE(properties_out.product_name() == std::string(""));
}

TEST_F(AmlG12TdmDaiTest, GetPropertiesInputDai) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::AmlConfig metadata = {};
  metadata.is_input = true;
  const std::string kTestString("test product");
  strncpy(metadata.product_name, kTestString.c_str(), sizeof(metadata.product_name));
  metadata.mClockDivFactor = 10;
  metadata.sClockDivFactor = 25;
  metadata.ring_buffer.number_of_channels = 2;
  metadata.lanes_enable_mask[0] = 3;
  metadata.bus = metadata::AmlBus::TDM_C;
  metadata.version = metadata::AmlVersion::kS905D2G;
  metadata.dai.type = metadata::DaiType::I2s;
  metadata.dai.number_of_channels = 2;
  metadata.dai.bits_per_sample = 16;
  metadata.dai.bits_per_slot = 32;
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  auto dai = std::make_unique<TestAmlG12TdmDai>(fake_parent.get(), pdev_.proto());
  auto dai_proto = dai->GetProto();
  ASSERT_OK(dai->InitPDev());
  ASSERT_OK(dai->DdkAdd("test"));
  dai.release();  // Managed by the DDK.

  DaiClient client(&dai_proto);

  ::fuchsia::hardware::audio::DaiProperties properties_out;
  ASSERT_OK(client.dai_->GetProperties(&properties_out));
  ASSERT_TRUE(properties_out.is_input());
  ASSERT_TRUE(properties_out.product_name() == kTestString);
  ASSERT_TRUE(properties_out.manufacturer() == std::string(""));
}

TEST_F(AmlG12TdmDaiTest, RingBufferOperations) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::AmlConfig metadata = GetDefaultMetadata();
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  auto dai = std::make_unique<TestAmlG12TdmDai>(fake_parent.get(), pdev_.proto());
  auto dai_proto = dai->GetProto();
  ASSERT_OK(dai->InitPDev());
  ASSERT_OK(dai->DdkAdd("test"));
  dai.release();  // Managed by the DDK.

  DaiClient client(&dai_proto);

  // Get ring buffer formats.
  ::fuchsia::hardware::audio::Dai_GetRingBufferFormats_Result ring_buffer_formats_out;
  ASSERT_OK(client.dai_->GetRingBufferFormats(&ring_buffer_formats_out));
  auto& all_pcm_formats = ring_buffer_formats_out.response().ring_buffer_formats;
  auto& pcm_formats = all_pcm_formats[0].pcm_supported_formats();
  ASSERT_EQ(1, pcm_formats.channel_sets().size());
  ASSERT_EQ(metadata.ring_buffer.number_of_channels,
            pcm_formats.channel_sets()[0].attributes().size());
  ASSERT_EQ(1, pcm_formats.sample_formats().size());
  ASSERT_EQ(::fuchsia::hardware::audio::SampleFormat::PCM_SIGNED, pcm_formats.sample_formats()[0]);
  ASSERT_EQ(5, pcm_formats.frame_rates().size());
  ASSERT_EQ(8'000, pcm_formats.frame_rates()[0]);
  ASSERT_EQ(16'000, pcm_formats.frame_rates()[1]);
  ASSERT_EQ(32'000, pcm_formats.frame_rates()[2]);
  ASSERT_EQ(48'000, pcm_formats.frame_rates()[3]);
  ASSERT_EQ(96'000, pcm_formats.frame_rates()[4]);
  ASSERT_EQ(1, pcm_formats.bytes_per_sample().size());
  ASSERT_EQ(2, pcm_formats.bytes_per_sample()[0]);
  ASSERT_EQ(1, pcm_formats.valid_bits_per_sample().size());
  ASSERT_EQ(16, pcm_formats.valid_bits_per_sample()[0]);

  // Get DAI formats.
  ::fuchsia::hardware::audio::Dai_GetDaiFormats_Result dai_formats_out;
  ASSERT_OK(client.dai_->GetDaiFormats(&dai_formats_out));
  auto& all_dai_formats = dai_formats_out.response().dai_formats;
  ASSERT_EQ(1, all_dai_formats.size());
  auto& dai_formats = all_dai_formats[0];
  ASSERT_EQ(1, dai_formats.number_of_channels.size());
  ASSERT_EQ(metadata.dai.number_of_channels, dai_formats.number_of_channels[0]);
  ASSERT_EQ(1, dai_formats.sample_formats.size());
  ASSERT_EQ(::fuchsia::hardware::audio::DaiSampleFormat::PCM_SIGNED, dai_formats.sample_formats[0]);
  ASSERT_EQ(5, pcm_formats.frame_rates().size());
  ASSERT_EQ(8'000, pcm_formats.frame_rates()[0]);
  ASSERT_EQ(16'000, pcm_formats.frame_rates()[1]);
  ASSERT_EQ(32'000, pcm_formats.frame_rates()[2]);
  ASSERT_EQ(48'000, pcm_formats.frame_rates()[3]);
  ASSERT_EQ(96'000, pcm_formats.frame_rates()[4]);
  ASSERT_EQ(1, dai_formats.bits_per_slot.size());
  ASSERT_EQ(32, dai_formats.bits_per_slot[0]);
  ASSERT_EQ(1, dai_formats.bits_per_sample.size());
  ASSERT_EQ(16, dai_formats.bits_per_sample[0]);

  // Create ring buffer, pick first ring buffer format and first DAI format.
  ::fuchsia::hardware::audio::DaiFormat dai_format = {};
  dai_format.number_of_channels = dai_formats.number_of_channels[0];
  dai_format.sample_format = dai_formats.sample_formats[0];
  dai_format.frame_format.set_frame_format_standard(
      dai_formats.frame_formats[0].frame_format_standard());
  dai_format.frame_rate = dai_formats.frame_rates[0];
  dai_format.bits_per_sample = dai_formats.bits_per_sample[0];
  dai_format.bits_per_slot = dai_formats.bits_per_slot[0];

  ::fuchsia::hardware::audio::Format ring_buffer_format = {};
  ring_buffer_format.mutable_pcm_format()->number_of_channels =
      pcm_formats.channel_sets()[0].attributes().size();
  ring_buffer_format.mutable_pcm_format()->sample_format = pcm_formats.sample_formats()[0];
  ring_buffer_format.mutable_pcm_format()->frame_rate = pcm_formats.frame_rates()[0];
  ring_buffer_format.mutable_pcm_format()->bytes_per_sample = pcm_formats.bytes_per_sample()[0];
  ring_buffer_format.mutable_pcm_format()->valid_bits_per_sample =
      pcm_formats.valid_bits_per_sample()[0];

  // Check ring buffer properties
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    ::fidl::InterfaceRequest<::fuchsia::hardware::audio::RingBuffer> ring_buffer_intf;
    ring_buffer_intf.set_channel(std::move(remote));

    client.dai_->CreateRingBuffer(std::move(dai_format), std::move(ring_buffer_format),
                                  std::move(ring_buffer_intf));

    ::fuchsia::hardware::audio::RingBuffer_SyncProxy ring_buffer(std::move(local));
    ::fuchsia::hardware::audio::RingBufferProperties properties;
    ASSERT_OK(ring_buffer.GetProperties(&properties));

    EXPECT_EQ(properties.fifo_depth(), 1024);
    EXPECT_EQ(properties.external_delay(), 0);
    EXPECT_TRUE(properties.needs_cache_flush_or_invalidate());
  }

  // Check delay state.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    ::fidl::InterfaceRequest<::fuchsia::hardware::audio::RingBuffer> ring_buffer_intf;
    ring_buffer_intf.set_channel(std::move(remote));

    client.dai_->CreateRingBuffer(std::move(dai_format), std::move(ring_buffer_format),
                                  std::move(ring_buffer_intf));

    ::fuchsia::hardware::audio::RingBuffer_SyncProxy ring_buffer(std::move(local));
    ::fuchsia::hardware::audio::DelayInfo delay_info;
    ASSERT_OK(ring_buffer.WatchDelayInfo(&delay_info));

    EXPECT_FALSE(delay_info.has_external_delay());
    // Based on FIFO depth = 1024 and
    // PCM format = frame size of 4 bytes (256 frames per fifo) and frame rate 8'000.
    // Hence a delay 256 / 8kHz = 32 msecs.
    EXPECT_EQ(delay_info.internal_delay(), 32'000'000);
  }

  // GetVmo then loose channel.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    ::fidl::InterfaceRequest<::fuchsia::hardware::audio::RingBuffer> ring_buffer_intf;
    ring_buffer_intf.set_channel(std::move(remote));

    client.dai_->CreateRingBuffer(std::move(dai_format), std::move(ring_buffer_format),
                                  std::move(ring_buffer_intf));

    ::fuchsia::hardware::audio::RingBuffer_SyncProxy ring_buffer(std::move(local));
    ::fuchsia::hardware::audio::RingBuffer_GetVmo_Result out_result = {};
    ASSERT_OK(ring_buffer.GetVmo(8192, 0, &out_result));
    ZX_ASSERT(out_result.response().num_frames == 8192);
    ZX_ASSERT(out_result.response().ring_buffer.is_valid());

    int64_t out_start_time = 0;
    ring_buffer.Start(&out_start_time);
    // Must fail, already started.
    ASSERT_NOT_OK(ring_buffer.GetVmo(8192, 0, &out_result));

    ring_buffer.Stop();
    // Must still fail, we lost the channel.
    ASSERT_NOT_OK(ring_buffer.GetVmo(4096, 0, &out_result));
  }

  // GetVmo multiple times.
  {
    zx::channel local, remote;
    ASSERT_OK(zx::channel::create(0, &local, &remote));
    ::fidl::InterfaceRequest<::fuchsia::hardware::audio::RingBuffer> ring_buffer_intf;
    ring_buffer_intf.set_channel(std::move(remote));

    client.dai_->CreateRingBuffer(std::move(dai_format), std::move(ring_buffer_format),
                                  std::move(ring_buffer_intf));
    ::fuchsia::hardware::audio::RingBuffer_SyncProxy ring_buffer(std::move(local));
    ::fuchsia::hardware::audio::RingBuffer_GetVmo_Result out_result = {};
    ASSERT_OK(ring_buffer.GetVmo(1, 0, &out_result));
    // 2 x 16 bits samples = 4 bytes frames, and must align to HW buffer (64 bits), so we need 2.
    ZX_ASSERT(out_result.response().num_frames == 2);
    ZX_ASSERT(out_result.response().ring_buffer.is_valid());

    int64_t out_start_time = 0;
    ring_buffer.Start(&out_start_time);
    ring_buffer.Stop();
    ASSERT_OK(ring_buffer.GetVmo(1, 0, &out_result));
    // 2 x 16 bits samples = 4 bytes frames, and must align to HW buffer (64 bits), so we need 2.
    ZX_ASSERT(out_result.response().num_frames == 2);
    ZX_ASSERT(out_result.response().ring_buffer.is_valid());
  }
}

TEST_F(AmlG12TdmDaiTest, ClientCloseDaiChannel) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::AmlConfig metadata = GetDefaultMetadata();
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  auto dai = std::make_unique<TestAmlG12TdmDai>(fake_parent.get(), pdev_.proto());
  ASSERT_OK(dai->InitPDev());
  ASSERT_OK(dai->DdkAdd("test"));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  TestAmlG12TdmDai* test_dev = child_dev->GetDeviceContext<TestAmlG12TdmDai>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_audio::DaiConnector>();
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_audio::DaiConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::DaiConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto endpoints2 = fidl::CreateEndpoints<fuchsia_hardware_audio::Dai>();
  fidl::WireSyncClient client_wrap{std::move(endpoints->client)};
  ASSERT_OK(client_wrap->Connect(std::move(endpoints2->server)));
  fidl::WireSyncClient client{std::move(endpoints2->client)};

  auto supported_formats_ring_buffer = client->GetRingBufferFormats();
  ASSERT_OK(supported_formats_ring_buffer.status());
  auto supported_formats_dai = client->GetDaiFormats();
  ASSERT_OK(supported_formats_dai.status());
  auto endpoints3 = fidl::CreateEndpoints<fuchsia_hardware_audio::RingBuffer>();
  ASSERT_OK(endpoints3.status_value());
  fidl::Arena allocator;
  fuchsia_hardware_audio::wire::Format ring_buffer_format(allocator);
  ring_buffer_format.set_pcm_format(allocator, GetDefaultPcmFormat());
  fuchsia_hardware_audio::wire::DaiFormat dai_format = GetDefaultDaiFormat();
  ASSERT_OK(
      client->CreateRingBuffer(dai_format, ring_buffer_format, std::move(endpoints3->server)));

  auto vmo =
      fidl::WireCall<fuchsia_hardware_audio::RingBuffer>(endpoints3->client)->GetVmo(8192, 0);
  ASSERT_OK(vmo.status());

  auto start_time = fidl::WireCall<fuchsia_hardware_audio::RingBuffer>(endpoints3->client)->Start();
  ASSERT_OK(start_time.status());

  // Close DAI channel.
  client = {};

  dai->WaitUntilStopped();
  dai.release();  // Managed by the DDK.
}

TEST_F(AmlG12TdmDaiTest, ClientCloseRingBufferChannel) {
  auto fake_parent = MockDevice::FakeRootParent();
  metadata::AmlConfig metadata = GetDefaultMetadata();
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  auto dai = std::make_unique<TestAmlG12TdmDai>(fake_parent.get(), pdev_.proto());
  ASSERT_OK(dai->InitPDev());
  ASSERT_OK(dai->DdkAdd("test"));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  TestAmlG12TdmDai* test_dev = child_dev->GetDeviceContext<TestAmlG12TdmDai>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_audio::DaiConnector>();
  std::optional<fidl::ServerBindingRef<fuchsia_hardware_audio::DaiConnector>> binding;
  binding = fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::DaiConnector>>(
      loop.dispatcher(), std::move(endpoints->server), test_dev);
  loop.StartThread("test-server");

  auto endpoints2 = fidl::CreateEndpoints<fuchsia_hardware_audio::Dai>();
  fidl::WireSyncClient client_wrap{std::move(endpoints->client)};
  ASSERT_OK(client_wrap->Connect(std::move(endpoints2->server)));
  fidl::WireSyncClient client{std::move(endpoints2->client)};

  auto supported_formats_ring_buffer = client->GetRingBufferFormats();
  ASSERT_OK(supported_formats_ring_buffer.status());
  auto supported_formats_dai = client->GetDaiFormats();
  ASSERT_OK(supported_formats_dai.status());
  auto endpoints3 = fidl::CreateEndpoints<fuchsia_hardware_audio::RingBuffer>();
  ASSERT_OK(endpoints3.status_value());
  fidl::Arena allocator;
  fuchsia_hardware_audio::wire::Format ring_buffer_format(allocator);
  ring_buffer_format.set_pcm_format(allocator, GetDefaultPcmFormat());
  fuchsia_hardware_audio::wire::DaiFormat dai_format = GetDefaultDaiFormat();
  ASSERT_OK(
      client->CreateRingBuffer(dai_format, ring_buffer_format, std::move(endpoints3->server)));

  auto vmo =
      fidl::WireCall<fuchsia_hardware_audio::RingBuffer>(endpoints3->client)->GetVmo(8192, 0);
  ASSERT_OK(vmo.status());

  auto start_time = fidl::WireCall<fuchsia_hardware_audio::RingBuffer>(endpoints3->client)->Start();
  ASSERT_OK(start_time.status());

  // Close RingBuffer channel.
  endpoints3->client.reset();

  dai->WaitUntilStopped();
  dai.release();  // Managed by the DDK.
}

}  // namespace audio::aml_g12

// Redefine PDevMakeMmioBufferWeak per the recommendation in pdev.h.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* test_harness = reinterpret_cast<audio::aml_g12::FakeMmio*>(pdev_mmio.offset);
  mmio->emplace(test_harness->mmio());
  return ZX_OK;
}
