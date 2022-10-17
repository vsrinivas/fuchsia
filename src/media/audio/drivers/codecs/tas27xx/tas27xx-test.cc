// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas27xx.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ddk/metadata.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-helper.h>
#include <lib/sync/completion.h>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace audio {

audio::DaiFormat GetDefaultDaiFormat() {
  return {
      .number_of_channels = 2,
      .channels_to_use_bitmask = 2,  // Use one channel (right) in this mono codec.
      .sample_format = SampleFormat::PCM_SIGNED,
      .frame_format = FrameFormat::I2S,
      .frame_rate = 24'000,
      .bits_per_slot = 32,
      .bits_per_sample = 16,
  };
}

struct Tas27xxCodec : public Tas27xx {
  explicit Tas27xxCodec(zx_device_t* parent, ddk::I2cChannel i2c, ddk::GpioProtocolClient fault)
      : Tas27xx(parent, std::move(i2c), std::move(fault), true, true) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
  inspect::Inspector& inspect() { return Tas27xx::inspect(); }
};

class Tas27xxTest : public inspect::InspectTestHelper, public zxtest::Test {
 public:
  void SetUp() override {
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    ASSERT_TRUE(endpoints.is_ok());

    fidl::BindServer<mock_i2c::MockI2c>(loop_.dispatcher(), std::move(endpoints->server),
                                        &mock_i2c_);

    mock_i2c_client_ = std::move(endpoints->client);
    EXPECT_OK(loop_.StartThread());
  }

 protected:
  mock_i2c::MockI2c mock_i2c_;
  fidl::ClientEnd<fuchsia_hardware_i2c::Device> mock_i2c_client_;

 private:
  async::Loop loop_{&kAsyncLoopConfigNeverAttachToThread};
};

TEST_F(Tas27xxTest, CodecInitGood) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  ddk::MockGpio mock_fault;
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
      fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

TEST_F(Tas27xxTest, CodecInitBad) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  ddk::MockGpio mock_fault;
  // Error when getting the interrupt.
  mock_fault.ExpectGetInterrupt(ZX_ERR_INTERNAL, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_EQ(ZX_ERR_INTERNAL,
            SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
                fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

TEST_F(Tas27xxTest, CodecGetInfo) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  ddk::MockGpio mock_fault;
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
      fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas27xxCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  auto info = client.GetInfo();
  ASSERT_EQ(info->unique_id.compare(""), 0);
  ASSERT_EQ(info->manufacturer.compare("Texas Instruments"), 0);
  ASSERT_EQ(info->product_name.compare("TAS2770"), 0);

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

TEST_F(Tas27xxTest, CodecReset) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  // Reset by the call to Reset.
  mock_i2c_
      .ExpectWriteStop({0x01, 0x01}, ZX_ERR_INTERNAL)  // SW_RESET error, will retry.
      .ExpectWriteStop({0x01, 0x01}, ZX_OK)            // SW_RESET.
      .ExpectWriteStop({0x02, 0x0e})                   // PWR_CTL stopped.
      .ExpectWriteStop({0x3c, 0x10})                   // CLOCK_CFG.
      .ExpectWriteStop({0x0a, 0x07})                   // SetRate.
      .ExpectWriteStop({0x0c, 0x22})                   // TDM_CFG2.
      .ExpectWriteStop({0x0e, 0x02})                   // TDM_CFG4.
      .ExpectWriteStop({0x0f, 0x44})                   // TDM_CFG5.
      .ExpectWriteStop({0x10, 0x40})                   // TDM_CFG6.
      .ExpectWrite({0x24})
      .ExpectReadStop({0x00})  // INT_LTCH0.
      .ExpectWrite({0x25})
      .ExpectReadStop({0x00})  // INT_LTCH1.
      .ExpectWrite({0x26})
      .ExpectReadStop({0x00})          // INT_LTCH2.
      .ExpectWriteStop({0x20, 0xf8})   // INT_MASK0.
      .ExpectWriteStop({0x21, 0xff})   // INT_MASK1.
      .ExpectWriteStop({0x30, 0x01})   // INT_CFG.
      .ExpectWriteStop({0x05, 0x3c})   // -30dB.
      .ExpectWriteStop({0x02, 0x0e});  // PWR_CTL stopped.

  ddk::MockGpio mock_fault;
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
      fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas27xxCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  ASSERT_OK(client.Reset());

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

// This test is disabled because it relies on a timeout expectation that would create flakes.
TEST_F(Tas27xxTest, DISABLED_CodecResetDueToErrorState) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  // Set gain state.
  mock_i2c_
      .ExpectWriteStop({0x05, 0x40})   // -32dB.
      .ExpectWriteStop({0x02, 0x0d});  // PWR_CTL stopped.

  // Set DAI format.
  mock_i2c_
      .ExpectWriteStop({0x0a, 0x07})   // SetRate 48k.
      .ExpectWriteStop({0x0c, 0x22});  // SetTdmSlots right.

  // Start.
  mock_i2c_.ExpectWriteStop({0x02, 0x00});  // PWR_CTL started.

  // Check for error state.
  mock_i2c_.ExpectWrite({0x02}).ExpectReadStop({0x02});  // PRW_CTL in shutdown.

  // Read state to report.
  mock_i2c_.ExpectWrite({0x24})
      .ExpectReadStop({0x00})  // INT_LTCH0.
      .ExpectWrite({0x25})
      .ExpectReadStop({0x00})  // INT_LTCH1.
      .ExpectWrite({0x26})
      .ExpectReadStop({0x00})  // INT_LTCH2.
      .ExpectWrite({0x29})
      .ExpectReadStop({0x00})  // TEMP_MSB.
      .ExpectWrite({0x2a})
      .ExpectReadStop({0x00})  // TEMP_LSB.
      .ExpectWrite({0x27})
      .ExpectReadStop({0x00})  // VBAT_MSB.
      .ExpectWrite({0x28})
      .ExpectReadStop({0x00});  // VBAT_LSB.

  // Reset.
  mock_i2c_
      .ExpectWriteStop({0x01, 0x01}, ZX_OK)  // SW_RESET.
      .ExpectWriteStop({0x02, 0x0d})         // PRW_CTL stopped.
      .ExpectWriteStop({0x3c, 0x10})         // CLOCK_CFG.
      .ExpectWriteStop({0x0a, 0x07})         // SetRate.
      .ExpectWriteStop({0x0c, 0x22})         // TDM_CFG2.
      .ExpectWriteStop({0x0e, 0x02})         // TDM_CFG4.
      .ExpectWriteStop({0x0f, 0x44})         // TDM_CFG5.
      .ExpectWriteStop({0x10, 0x40})         // TDM_CFG6.
      .ExpectWrite({0x24})
      .ExpectReadStop({0x00})  // INT_LTCH0.
      .ExpectWrite({0x25})
      .ExpectReadStop({0x00})  // INT_LTCH1.
      .ExpectWrite({0x26})
      .ExpectReadStop({0x00})          // INT_LTCH2.
      .ExpectWriteStop({0x20, 0xf8})   // INT_MASK0.
      .ExpectWriteStop({0x21, 0xff})   // INT_MASK1.
      .ExpectWriteStop({0x30, 0x01})   // INT_CFG.
      .ExpectWriteStop({0x05, 0x3c})   // -30dB, default.
      .ExpectWriteStop({0x02, 0x0d});  // PWR_CTL stopped.

  // Set gain state.
  mock_i2c_
      .ExpectWriteStop({0x05, 0x40})   // -32dB, old gain_state_.
      .ExpectWriteStop({0x02, 0x0d});  // PWR_CTL stopped.

  // Set DAI format.
  mock_i2c_
      .ExpectWriteStop({0x0a, 0x07})   // SetRate 48k.
      .ExpectWriteStop({0x0c, 0x22});  // SetTdmSlots right.

  // Start.
  mock_i2c_.ExpectWriteStop({0x02, 0x00});  // PWR_CTL started.

  ddk::MockGpio mock_fault;
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
      fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas27xxCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  client.SetGainState({
      .gain = -32.f,
      .muted = false,
      .agc_enabled = false,
  });

  DaiFormat format = GetDefaultDaiFormat();
  format.frame_rate = 48'000;
  ASSERT_OK(client.SetDaiFormat(std::move(format)));

  // Get into started state, so we can be in error state after the timeout.
  ASSERT_OK(client.Start());

  // Wait for the timeout to occur.
  constexpr int64_t kTimeoutSeconds = 30;
  zx::nanosleep(zx::deadline_after(zx::sec(kTimeoutSeconds)));

  // Make a 2-way call to make sure the server (we know single threaded) completed previous calls.
  ASSERT_OK(client.GetInfo());

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

TEST_F(Tas27xxTest, ExternalConfig) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  metadata::ti::TasConfig metadata = {};
  metadata.number_of_writes1 = 2;
  metadata.init_sequence1[0].address = 0x12;
  metadata.init_sequence1[0].value = 0x34;
  metadata.init_sequence1[1].address = 0x56;
  metadata.init_sequence1[1].value = 0x78;
  metadata.number_of_writes2 = 3;
  metadata.init_sequence2[0].address = 0x11;
  metadata.init_sequence2[0].value = 0x22;
  metadata.init_sequence2[1].address = 0x33;
  metadata.init_sequence2[1].value = 0x44;
  metadata.init_sequence2[2].address = 0x55;
  metadata.init_sequence2[2].value = 0x66;
  fake_parent->SetMetadata(DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata));

  // Reset by the call to Reset.
  mock_i2c_
      .ExpectWriteStop({0x01, 0x01}, ZX_ERR_INTERNAL)  // SW_RESET error, will retry.
      .ExpectWriteStop({0x01, 0x01}, ZX_OK)            // SW_RESET.
      .ExpectWriteStop({0x12, 0x34})                   // External config.
      .ExpectWriteStop({0x56, 0x78})                   // External config.
      .ExpectWriteStop({0x11, 0x22})                   // External config.
      .ExpectWriteStop({0x33, 0x44})                   // External config.
      .ExpectWriteStop({0x55, 0x66})                   // External config.
      .ExpectWriteStop({0x02, 0x0e})                   // PWR_CTL stopped.
      .ExpectWriteStop({0x3c, 0x10})                   // CLOCK_CFG.
      .ExpectWriteStop({0x0a, 0x07})                   // SetRate.
      .ExpectWriteStop({0x0c, 0x22})                   // TDM_CFG2.
      .ExpectWriteStop({0x0e, 0x02})                   // TDM_CFG4.
      .ExpectWriteStop({0x0f, 0x44})                   // TDM_CFG5.
      .ExpectWriteStop({0x10, 0x40})                   // TDM_CFG6.
      .ExpectWrite({0x24})
      .ExpectReadStop({0x00})  // INT_LTCH0.
      .ExpectWrite({0x25})
      .ExpectReadStop({0x00})  // INT_LTCH1.
      .ExpectWrite({0x26})
      .ExpectReadStop({0x00})          // INT_LTCH2.
      .ExpectWriteStop({0x20, 0xf8})   // INT_MASK0.
      .ExpectWriteStop({0x21, 0xff})   // INT_MASK1.
      .ExpectWriteStop({0x30, 0x01})   // INT_CFG.
      .ExpectWriteStop({0x05, 0x3c})   // -30dB.
      .ExpectWriteStop({0x02, 0x0e});  // PWR_CTL stopped.

  ddk::MockGpio mock_fault;
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
      fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas27xxCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  ASSERT_OK(client.Reset());

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

TEST_F(Tas27xxTest, CodecBridgedMode) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  ddk::MockGpio mock_fault;
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
      fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas27xxCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  {
    auto bridgeable = client.IsBridgeable();
    ASSERT_FALSE(bridgeable.value());
  }
  client.SetBridgedMode(false);

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

TEST_F(Tas27xxTest, CodecDaiFormat) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  ddk::MockGpio mock_fault;
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
      fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas27xxCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  // We complete all i2c mock setup before executing server methods in a different thread.
  mock_i2c_
      .ExpectWriteStop({0x0a, 0x07})   // SetRate 48k.
      .ExpectWriteStop({0x0c, 0x22})   // SetTdmSlots right.
      .ExpectWriteStop({0x0a, 0x09})   // SetRate 96k.
      .ExpectWriteStop({0x0c, 0x12});  // SetTdmSlots left.

  // Check getting DAI formats.
  {
    auto formats = client.GetDaiFormats();
    ASSERT_EQ(formats.value().number_of_channels.size(), 1);
    ASSERT_EQ(formats.value().number_of_channels[0], 2);
    ASSERT_EQ(formats.value().sample_formats.size(), 1);
    ASSERT_EQ(formats.value().sample_formats[0], SampleFormat::PCM_SIGNED);
    ASSERT_EQ(formats.value().frame_formats.size(), 1);
    ASSERT_EQ(formats.value().frame_formats[0], FrameFormat::I2S);
    ASSERT_EQ(formats.value().frame_rates.size(), 2);
    ASSERT_EQ(formats.value().frame_rates[0], 48000);
    ASSERT_EQ(formats.value().frame_rates[1], 96000);
    ASSERT_EQ(formats.value().bits_per_slot.size(), 1);
    ASSERT_EQ(formats.value().bits_per_slot[0], 32);
    ASSERT_EQ(formats.value().bits_per_sample.size(), 1);
    ASSERT_EQ(formats.value().bits_per_sample[0], 16);
  }

  // Check inspect state.
  ASSERT_NO_FATAL_FAILURE(ReadInspect(codec->inspect().DuplicateVmo()));
  auto* simple_codec = hierarchy().GetByPath({"simple_codec"});
  ASSERT_TRUE(simple_codec);
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_codec->node(), "state", inspect::StringPropertyValue("created")));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_codec->node(), "start_time", inspect::IntPropertyValue(0)));
  ASSERT_NO_FATAL_FAILURE(CheckProperty(simple_codec->node(), "manufacturer",
                                        inspect::StringPropertyValue("Texas Instruments")));
  ASSERT_NO_FATAL_FAILURE(
      CheckProperty(simple_codec->node(), "product", inspect::StringPropertyValue("TAS2770")));

  // Check setting DAI formats.
  {
    DaiFormat format = GetDefaultDaiFormat();
    format.frame_rate = 48'000;
    auto formats = client.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    zx::result<CodecFormatInfo> codec_format_info = client.SetDaiFormat(std::move(format));
    ASSERT_OK(codec_format_info.status_value());
    EXPECT_EQ(zx::usec(5'300).get(), codec_format_info->turn_on_delay());
    EXPECT_EQ(zx::usec(4'700).get(), codec_format_info->turn_off_delay());
  }

  {
    DaiFormat format = GetDefaultDaiFormat();
    format.frame_rate = 96'000;
    format.channels_to_use_bitmask = 1;  // Use one channel (left) in this mono codec.
    auto formats = client.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    zx::result<CodecFormatInfo> codec_format_info = client.SetDaiFormat(std::move(format));
    ASSERT_OK(codec_format_info.status_value());
    EXPECT_EQ(zx::usec(5'300).get(), codec_format_info->turn_on_delay());
    EXPECT_EQ(zx::usec(4'700).get(), codec_format_info->turn_off_delay());
  }

  {
    DaiFormat format = GetDefaultDaiFormat();
    format.frame_rate = 192'000;
    auto formats = client.GetDaiFormats();
    ASSERT_FALSE(IsDaiFormatSupported(format, formats.value()));
    ASSERT_NOT_OK(client.SetDaiFormat(std::move(format)));
  }

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

TEST_F(Tas27xxTest, CodecGain) {
  auto fake_parent = MockDevice::FakeRootParent();
  zx::interrupt irq;
  ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq));

  ddk::MockGpio mock_fault;
  mock_fault.ExpectGetInterrupt(ZX_OK, ZX_INTERRUPT_MODE_EDGE_LOW, std::move(irq));

  ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Tas27xxCodec>(
      fake_parent.get(), std::move(mock_i2c_client_), mock_fault.GetProto()));
  auto* child_dev = fake_parent->GetLatestChild();
  ASSERT_NOT_NULL(child_dev);
  auto codec = child_dev->GetDeviceContext<Tas27xxCodec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  // We complete all i2c mock setup before executing server methods in a different thread.
  mock_i2c_
      .ExpectWriteStop({0x05, 0x40})   // -32dB.
      .ExpectWriteStop({0x02, 0x0e});  // PWR_CTL stopped.

  // Lower than min gain.
  mock_i2c_
      .ExpectWriteStop({0x05, 0xc8})   // -100dB.
      .ExpectWriteStop({0x02, 0x0e});  // PWR_CTL stopped.

  // Higher than max gain.
  mock_i2c_
      .ExpectWriteStop({0x05, 0x0})    // 0dB.
      .ExpectWriteStop({0x02, 0x0e});  // PWR_CTL stopped.

  // Reset and start so the codec is powered down by stop when muted.
  mock_i2c_
      .ExpectWriteStop({0x01, 0x01}, ZX_ERR_INTERNAL)  // SW_RESET error, will retry.
      .ExpectWriteStop({0x01, 0x01}, ZX_OK)            // SW_RESET.
      .ExpectWriteStop({0x02, 0x0e})                   // PWR_CTL stopped.
      .ExpectWriteStop({0x3c, 0x10})                   // CLOCK_CFG.
      .ExpectWriteStop({0x0a, 0x07})                   // SetRate.
      .ExpectWriteStop({0x0c, 0x22})                   // TDM_CFG2.
      .ExpectWriteStop({0x0e, 0x02})                   // TDM_CFG4.
      .ExpectWriteStop({0x0f, 0x44})                   // TDM_CFG5.
      .ExpectWriteStop({0x10, 0x40})                   // TDM_CFG6.
      .ExpectWrite({0x24})
      .ExpectReadStop({0x00})  // INT_LTCH0.
      .ExpectWrite({0x25})
      .ExpectReadStop({0x00})  // INT_LTCH1.
      .ExpectWrite({0x26})
      .ExpectReadStop({0x00})          // INT_LTCH2.
      .ExpectWriteStop({0x20, 0xf8})   // INT_MASK0.
      .ExpectWriteStop({0x21, 0xff})   // INT_MASK1.
      .ExpectWriteStop({0x30, 0x01})   // INT_CFG.
      .ExpectWriteStop({0x05, 0x3c})   // -30dB.
      .ExpectWriteStop({0x02, 0x0e});  // PWR_CTL stopped.

  // Start but muted.
  mock_i2c_.ExpectWriteStop({0x02, 0x01});  // PWR_CTL stopped due to mute state.

  // Unmute.
  mock_i2c_
      .ExpectWriteStop({0x05, 0x0})    // 0dB.
      .ExpectWriteStop({0x02, 0x00});  // PWR_CTL started.

  // Change gain, keep mute and AGC.
  client.SetGainState({
      .gain = -32.f,
      .muted = true,
      .agc_enabled = false,
  });
  // Change gain, keep mute and AGC.
  client.SetGainState({
      .gain = -999.f,
      .muted = true,
      .agc_enabled = false,
  });
  // Change gain, keep mute and AGC.
  client.SetGainState({
      .gain = 111.f,
      .muted = true,
      .agc_enabled = false,
  });

  // Get into reset ad started state, so mute powers the codec down.
  ASSERT_OK(client.Reset());
  ASSERT_OK(client.Start());
  // Change mute, keep gain and AGC.
  client.SetGainState({
      .gain = 111.f,
      .muted = false,
      .agc_enabled = false,
  });

  // Make a 2-wal call to make sure the server (we know single threaded) completed previous calls.
  ASSERT_OK(client.GetInfo());

  mock_i2c_.VerifyAndClear();
  mock_fault.VerifyAndClear();
}

}  // namespace audio
