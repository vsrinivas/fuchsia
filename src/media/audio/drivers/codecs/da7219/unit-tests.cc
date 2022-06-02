// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/simple-codec/simple-codec-client.h>
#include <lib/simple-codec/simple-codec-helper.h>

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/media/audio/drivers/codecs/da7219/da7219.h"

namespace audio {

using inspect::InspectTestHelper;

struct Da7219Codec : public Da7219 {
  explicit Da7219Codec(zx_device_t* parent, fidl::ClientEnd<fuchsia_hardware_i2c::Device> i2c,
                       zx::interrupt irq)
      : Da7219(parent, std::move(i2c), std::move(irq)) {}
  codec_protocol_t GetProto() { return {&this->codec_protocol_ops_, this}; }
};

class Da7219Test : public InspectTestHelper, public zxtest::Test {
 public:
  Da7219Test() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    // IDs check.
    mock_i2c_.ExpectWrite({0x81}).ExpectReadStop({0x23}, ZX_OK);
    mock_i2c_.ExpectWrite({0x82}).ExpectReadStop({0x93}, ZX_OK);
    mock_i2c_.ExpectWrite({0x83}).ExpectReadStop({0x02}, ZX_OK);

    fake_root_ = MockDevice::FakeRootParent();
    auto endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    EXPECT_TRUE(endpoints.is_ok());

    EXPECT_OK(loop_.StartThread());
    fidl::BindServer<mock_i2c::MockI2c>(loop_.dispatcher(), std::move(endpoints->server),
                                        &mock_i2c_);
    ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
    zx::interrupt irq2;
    ASSERT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &irq2));
    ASSERT_OK(SimpleCodecServer::CreateAndAddToDdk<Da7219Codec>(
        fake_root_.get(), std::move(endpoints->client), std::move(irq2)));
  }

  void TearDown() override { mock_i2c_.VerifyAndClear(); }

  zx::interrupt& irq() { return irq_; }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  mock_i2c::MockI2c mock_i2c_;
  async::Loop loop_;
  zx::interrupt irq_;
};

TEST_F(Da7219Test, GetInfo) {
  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  auto info = client.GetInfo();
  EXPECT_EQ(info.value().unique_id.compare(""), 0);
  EXPECT_EQ(info.value().manufacturer.compare("Dialog"), 0);
  EXPECT_EQ(info.value().product_name.compare("DA7219"), 0);
}

TEST_F(Da7219Test, Reset) {
  // Reset.
  mock_i2c_.ExpectWriteStop({0xfd, 0x01}, ZX_OK);  // Enable.
  mock_i2c_.ExpectWriteStop({0x20, 0x8c}, ZX_OK);  // PLL.
  mock_i2c_.ExpectWriteStop({0x47, 0xa0}, ZX_OK);  // Charge Pump enablement.
  mock_i2c_.ExpectWriteStop({0x69, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6a, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4b, 0x01}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4c, 0x01}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6e, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6f, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6b, 0x00}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWriteStop({0x6c, 0x00}, ZX_OK);  // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc6, 0xd7}, ZX_OK);  // Enable AAD.
  {
    // TODO(101483): Remove this workaround of a force unplugged report initially.
    mock_i2c_.ExpectWrite({0x6b})
        .ExpectReadStop({0xff}, ZX_OK)
        .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
    mock_i2c_.ExpectWrite({0x6c})
        .ExpectReadStop({0xff}, ZX_OK)
        .ExpectWriteStop({0x6c, 0x77}, ZX_OK);  // HP Routing (Right HP disabled).
  }
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);  // Check plug state.
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc4, 0x01}, ZX_OK);  // Unmask AAD (leave insert masked).
  mock_i2c_.ExpectWriteStop({0xc5, 0xff}, ZX_OK);  // Mask buttons.
  mock_i2c_.ExpectWriteStop({0xc3, 0xff}, ZX_OK);  // Clear buttons.

  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  [[maybe_unused]] auto unused = client.Reset();
}

TEST_F(Da7219Test, GoodSetDai) {
  // Set DAI.
  mock_i2c_.ExpectWriteStop({0x2d, 0x00}, ZX_OK);  // TDM mode disabled.
  mock_i2c_.ExpectWriteStop({0x2c, 0xa8}, ZX_OK);  // 24 bits per sample.

  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);
  {
    audio::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 3;
    format.sample_format = SampleFormat::PCM_SIGNED;
    format.frame_format = FrameFormat::I2S;
    format.frame_rate = 48000;
    format.bits_per_slot = 32;
    format.bits_per_sample = 24;
    auto formats = client.GetDaiFormats();
    ASSERT_TRUE(IsDaiFormatSupported(format, formats.value()));
    auto codec_format_info = client.SetDaiFormat(std::move(format));
    ASSERT_OK(codec_format_info.status_value());
    EXPECT_FALSE(codec_format_info->has_turn_off_delay());
    EXPECT_FALSE(codec_format_info->has_turn_on_delay());
  }
}

TEST_F(Da7219Test, PlugDetectInitiallyUnplugged) {
  // Reset.
  mock_i2c_.ExpectWriteStop({0xfd, 0x01}, ZX_OK);  // Enable.
  mock_i2c_.ExpectWriteStop({0x20, 0x8c}, ZX_OK);  // PLL.
  mock_i2c_.ExpectWriteStop({0x47, 0xa0}, ZX_OK);  // Charge Pump enablement.
  mock_i2c_.ExpectWriteStop({0x69, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6a, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4b, 0x01}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4c, 0x01}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6e, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6f, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6b, 0x00}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWriteStop({0x6c, 0x00}, ZX_OK);  // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc6, 0xd7}, ZX_OK);  // Enable AAD.
  {
    // TODO(101483): Remove this workaround of a force unplugged report initially.
    mock_i2c_.ExpectWrite({0x6b})
        .ExpectReadStop({0xff}, ZX_OK)
        .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
    mock_i2c_.ExpectWrite({0x6c})
        .ExpectReadStop({0xff}, ZX_OK)
        .ExpectWriteStop({0x6c, 0x77}, ZX_OK);  // HP Routing (Right HP disabled).
  }
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);  // Check plug state (unplugged).
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc4, 0x01}, ZX_OK);  // Unmask AAD (leave insert masked).
  mock_i2c_.ExpectWriteStop({0xc5, 0xff}, ZX_OK);  // Mask buttons.
  mock_i2c_.ExpectWriteStop({0xc3, 0xff}, ZX_OK);  // Clear buttons.

  // Plug detected from irq trigger, jack detect completed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x04}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Unplug detected from irq trigger, jack removed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  [[maybe_unused]] auto unused = client.Reset();

  // Initial Watch gets status from Reset.
  auto initial_plugged_state = client.WiredSharedClient().sync()->WatchPlugState();
  ASSERT_TRUE(initial_plugged_state.ok());
  ASSERT_FALSE(initial_plugged_state.value().plug_state.plugged());
  ASSERT_GT(initial_plugged_state.value().plug_state.plug_state_time(), 0);

  // Trigger IRQ and Watch for plugging the headset.
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));
  auto plugged_state = client.WiredSharedClient().sync()->WatchPlugState();
  ASSERT_TRUE(plugged_state.ok());
  ASSERT_TRUE(plugged_state.value().plug_state.plugged());
  ASSERT_GT(plugged_state.value().plug_state.plug_state_time(), 0);

  // Trigger Watch and IRQ for unplugging the headset.
  auto thread = std::thread([&] {
    auto plugged_state = client.WiredSharedClient().sync()->WatchPlugState();
    ASSERT_TRUE(plugged_state.ok());
    ASSERT_FALSE(plugged_state.value().plug_state.plugged());
    ASSERT_GT(plugged_state.value().plug_state.plug_state_time(), 0);
  });
  // Delay not required for the test to pass, it can trigger a failure if the tested code does not
  // handle clearing its callbacks correctly.
  zx::nanosleep(zx::deadline_after(zx::msec(1)));
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));
  thread.join();

  // To make sure the IRQ processing is completed in the server, make a 2-way call synchronously.
  auto info = client.GetInfo();
  EXPECT_EQ(info.value().product_name.compare("DA7219"), 0);
}

TEST_F(Da7219Test, PlugDetectInitiallyPlugged) {
  // Reset.
  mock_i2c_.ExpectWriteStop({0xfd, 0x01}, ZX_OK);  // Enable.
  mock_i2c_.ExpectWriteStop({0x20, 0x8c}, ZX_OK);  // PLL.
  mock_i2c_.ExpectWriteStop({0x47, 0xa0}, ZX_OK);  // Charge Pump enablement.
  mock_i2c_.ExpectWriteStop({0x69, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6a, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4b, 0x01}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4c, 0x01}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6e, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6f, 0x80}, ZX_OK);  // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6b, 0x00}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWriteStop({0x6c, 0x00}, ZX_OK);  // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc6, 0xd7}, ZX_OK);  // Enable AAD.
  {
    // TODO(101483): Remove this workaround of a force unplugged report initially.
    mock_i2c_.ExpectWrite({0x6b})
        .ExpectReadStop({0xff}, ZX_OK)
        .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
    mock_i2c_.ExpectWrite({0x6c})
        .ExpectReadStop({0xff}, ZX_OK)
        .ExpectWriteStop({0x6c, 0x77}, ZX_OK);  // HP Routing (Right HP disabled).
  }
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x01}, ZX_OK);  // Check plug state (plugged).
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc4, 0x01}, ZX_OK);  // Unmask AAD (leave insert masked).
  mock_i2c_.ExpectWriteStop({0xc5, 0xff}, ZX_OK);  // Mask buttons.
  mock_i2c_.ExpectWriteStop({0xc3, 0xff}, ZX_OK);  // Clear buttons.

  // Plug detected from irq trigger, jack detect completed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x04}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Unplug detected from irq trigger, jack removed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Plug detected from irq trigger, jack detect completed again.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x04}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  [[maybe_unused]] auto unused = client.Reset();

  // Initial Watch gets status from Reset.
  auto initial_plugged_state = client.WiredSharedClient().sync()->WatchPlugState();
  ASSERT_TRUE(initial_plugged_state.ok());
  ASSERT_TRUE(initial_plugged_state.value().plug_state.plugged());
  ASSERT_GT(initial_plugged_state.value().plug_state.plug_state_time(), 0);

  // Trigger IRQ for a still plugged headset so we can't Watch (there would be no reply).
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));

  // Trigger Watch and IRQ for unplugging the headset.
  auto thread = std::thread([&] {
    auto plugged_state = client.WiredSharedClient().sync()->WatchPlugState();
    ASSERT_TRUE(plugged_state.ok());
    ASSERT_FALSE(plugged_state.value().plug_state.plugged());
    ASSERT_GT(plugged_state.value().plug_state.plug_state_time(), 0);
  });
  // Delay not required for the test to pass, it can trigger a failure if the tested code does not
  // handle clearing its callbacks correctly.
  zx::nanosleep(zx::deadline_after(zx::msec(1)));
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));
  thread.join();

  // Trigger IRQ for plugging the headset again.
  auto thread2 = std::thread([&] {
    auto plugged_state = client.WiredSharedClient().sync()->WatchPlugState();
    ASSERT_TRUE(plugged_state.ok());
    ASSERT_TRUE(plugged_state.value().plug_state.plugged());
    ASSERT_GT(plugged_state.value().plug_state.plug_state_time(), 0);
  });
  // Delay not required for the test to pass, it can trigger a failure if the tested code does not
  // handle clearing its callbacks correctly.
  zx::nanosleep(zx::deadline_after(zx::msec(1)));
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));
  thread2.join();

  // To make sure the IRQ processing is completed in the server, make a 2-way call synchronously.
  auto info = client.GetInfo();
  EXPECT_EQ(info.value().product_name.compare("DA7219"), 0);
}

TEST_F(Da7219Test, PlugDetectWatchBeforeReset) {
  // Unplug detected from irq trigger, jack removed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Plug detected from irq trigger, jack detect completed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x04}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  auto* child_dev = fake_root_->GetLatestChild();
  auto codec = child_dev->GetDeviceContext<Da7219Codec>();
  auto codec_proto = codec->GetProto();
  SimpleCodecClient client;
  client.SetProtocol(&codec_proto);

  // When a Watch is issued before Reset the driver has no choice but to reply with some default
  // initialized values (in this case unpluged at time "0").
  auto initial_plugged_state = client.WiredSharedClient().sync()->WatchPlugState();
  ASSERT_TRUE(initial_plugged_state.ok());
  ASSERT_FALSE(initial_plugged_state.value().plug_state.plugged());
  ASSERT_EQ(initial_plugged_state.value().plug_state.plug_state_time(), 0);

  // Trigger IRQ for unplugging the headset.
  // No additional watch reply triggered since it is the same the initial plugged state.
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));

  // Trigger IRQ and Watch for plugging the headset.
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));
  // This last 2-way sync call makes sure the IRQ processing is completed in the server.
  auto plugged_state = client.WiredSharedClient().sync()->WatchPlugState();
  ASSERT_TRUE(plugged_state.ok());
  ASSERT_TRUE(plugged_state.value().plug_state.plugged());
}

}  // namespace audio
