// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <lib/async-loop/cpp/loop.h>
#include <lib/mock-i2c/mock-i2c.h>
#include <lib/zx/clock.h>

#include <memory>

#include <sdk/lib/inspect/testing/cpp/zxtest/inspect.h>
#include <zxtest/zxtest.h>

#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/media/audio/drivers/codecs/da7219/da7219.h"

namespace audio::da7219 {

class Da7219Test : public zxtest::Test {
 public:
  Da7219Test() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  void SetUp() override {
    // IDs check.
    mock_i2c_.ExpectWrite({0x81}).ExpectReadStop({0x23}, ZX_OK);
    mock_i2c_.ExpectWrite({0x82}).ExpectReadStop({0x93}, ZX_OK);
    mock_i2c_.ExpectWrite({0x83}).ExpectReadStop({0x02}, ZX_OK);

    fake_root_ = MockDevice::FakeRootParent();
    auto i2c_endpoints = fidl::CreateEndpoints<fuchsia_hardware_i2c::Device>();
    EXPECT_TRUE(i2c_endpoints.is_ok());

    EXPECT_OK(loop_.StartThread());
    fidl::BindServer<mock_i2c::MockI2c>(loop_.dispatcher(), std::move(i2c_endpoints->server),
                                        &mock_i2c_);
    ASSERT_OK(zx::interrupt::create(zx::resource(), 0, ZX_INTERRUPT_VIRTUAL, &irq_));
    zx::interrupt irq2;
    ASSERT_OK(irq_.duplicate(ZX_RIGHT_SAME_RIGHTS, &irq2));

    core_ = std::make_shared<Core>(std::move(i2c_endpoints->client), std::move(irq2));
    ASSERT_OK(core_->Initialize());

    auto codec_connector_endpoints =
        fidl::CreateEndpoints<fuchsia_hardware_audio::CodecConnector>();
    EXPECT_TRUE(codec_connector_endpoints.is_ok());
    codec_connector_ = fidl::WireSyncClient(std::move(codec_connector_endpoints->client));

    auto output_device = std::make_unique<Driver>(fake_root_.get(), core_, false);

    fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::CodecConnector>>(
        core_->dispatcher(), std::move(codec_connector_endpoints->server), output_device.get());

    auto codec_endpoints = fidl::CreateEndpoints<fuchsia_hardware_audio::Codec>();
    EXPECT_TRUE(codec_endpoints.is_ok());
    codec_ = fidl::WireSyncClient(std::move(codec_endpoints->client));

    [[maybe_unused]] auto unused = codec_connector_->Connect(std::move(codec_endpoints->server));

    ASSERT_OK(output_device->DdkAdd(ddk::DeviceAddArgs("DA7219-output")));
    output_device.release();
  }

  void TearDown() override {
    mock_i2c_.ExpectWriteStop({0xfd, 0x00}, ZX_OK);  // Disable as part of unbind's shutdown.
    auto* child_dev = fake_root_->GetLatestChild();
    child_dev->UnbindOp();
    mock_i2c_.VerifyAndClear();
  }

  void CheckDaiStateWithRate(uint32_t frame_rate) {
    mock_i2c_.ExpectWriteStop({0x2d, 0x43}, ZX_OK);  // TDM mode disabled, enable, L/R enabled.
    mock_i2c_.ExpectWriteStop({0x2c, 0xa8}, ZX_OK);  // DAI enable 24 bits per sample.

    fuchsia_hardware_audio::wire::DaiFormat format = {};
    format.number_of_channels = 2;
    format.channels_to_use_bitmask = 3;
    format.sample_format = fuchsia_hardware_audio::wire::DaiSampleFormat::kPcmSigned;
    format.frame_format = fuchsia_hardware_audio::wire::DaiFrameFormat::WithFrameFormatStandard(
        fuchsia_hardware_audio::DaiFrameFormatStandard::kI2S);
    format.frame_rate = frame_rate;
    format.bits_per_slot = 32;
    format.bits_per_sample = 24;
    auto codec_format_info = codec_->SetDaiFormat(std::move(format));
    ASSERT_OK(codec_format_info.status());
    EXPECT_FALSE(codec_format_info.value().value()->state.has_turn_off_delay());
    EXPECT_FALSE(codec_format_info.value().value()->state.has_turn_on_delay());
  }

  zx::interrupt& irq() { return irq_; }

 protected:
  std::shared_ptr<zx_device> fake_root_;
  mock_i2c::MockI2c mock_i2c_;
  async::Loop loop_;
  zx::interrupt irq_;
  std::shared_ptr<Core> core_;
  fidl::WireSyncClient<fuchsia_hardware_audio::CodecConnector> codec_connector_;
  fidl::WireSyncClient<fuchsia_hardware_audio::Codec> codec_;
};

TEST_F(Da7219Test, GetInfo) {
  auto info = codec_->GetInfo();
  EXPECT_EQ(info->info.unique_id.size(), 0);
  EXPECT_EQ(std::string(info->info.manufacturer.data()).compare("Dialog"), 0);
  EXPECT_EQ(std::string(info->info.product_name.data()).compare("DA7219"), 0);
}

TEST_F(Da7219Test, Reset) {
  // Reset.
  mock_i2c_.ExpectWriteStop({0xfd, 0x01}, ZX_OK);               // Enable.
  mock_i2c_.ExpectWriteStop({0x20, 0x8c}, ZX_OK);               // PLL.
  mock_i2c_.ExpectWriteStop({0x47, 0xa0}, ZX_OK);               // Charge Pump enablement.
  mock_i2c_.ExpectWriteStop({0x69, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6a, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4b, 0x01}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4c, 0x01}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6e, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6f, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6b, 0x00}, ZX_OK);               // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWriteStop({0x6c, 0x00}, ZX_OK);               // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0x39, 0x06}, ZX_OK);               // Input mic gain.
  mock_i2c_.ExpectWriteStop({0x63, 0x80}, ZX_OK);               // Input mic control.
  mock_i2c_.ExpectWriteStop({0x33, 0x01}, ZX_OK);               // Input mic select.
  mock_i2c_.ExpectWriteStop({0x65, 0x88}, ZX_OK);               // Input mixin control.
  mock_i2c_.ExpectWriteStop({0x67, 0x80}, ZX_OK);               // Input ADC control.
  mock_i2c_.ExpectWriteStop({0x2a, 0x00}, ZX_OK);               // Input Digital routing.
  mock_i2c_.ExpectWriteStop({0xc6, 0xd7}, ZX_OK);               // Enable AAD.
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

  [[maybe_unused]] auto unused = codec_->Reset();
}

TEST_F(Da7219Test, GoodSetDai48kHz) {
  mock_i2c_.ExpectWriteStop({0x2c, 0x00}, ZX_OK);  // DAI disable.
  mock_i2c_.ExpectWriteStop({0x17, 0x0b}, ZX_OK);  // 48kHz.
  CheckDaiStateWithRate(48'000);
}

TEST_F(Da7219Test, GoodSetDai8kHz) {
  mock_i2c_.ExpectWriteStop({0x2c, 0x00}, ZX_OK);  // DAI disable.
  mock_i2c_.ExpectWriteStop({0x17, 0x01}, ZX_OK);  // 8kHz.
  CheckDaiStateWithRate(8'000);
}

TEST_F(Da7219Test, GoodSetDai96kHz) {
  mock_i2c_.ExpectWriteStop({0x2c, 0x00}, ZX_OK);  // DAI disable.
  mock_i2c_.ExpectWriteStop({0x17, 0x0f}, ZX_OK);  // 96kHz.
  CheckDaiStateWithRate(96'000);
}

TEST_F(Da7219Test, GoodSetDai44100Hz) {
  mock_i2c_.ExpectWriteStop({0x2c, 0x00}, ZX_OK);  // DAI disable.
  mock_i2c_.ExpectWriteStop({0x17, 0x0a}, ZX_OK);  // 44.1kHz.
  CheckDaiStateWithRate(44'100);
}

TEST_F(Da7219Test, PlugDetectInitiallyUnplugged) {
  // Reset.
  mock_i2c_.ExpectWriteStop({0xfd, 0x01}, ZX_OK);               // Enable.
  mock_i2c_.ExpectWriteStop({0x20, 0x8c}, ZX_OK);               // PLL.
  mock_i2c_.ExpectWriteStop({0x47, 0xa0}, ZX_OK);               // Charge Pump enablement.
  mock_i2c_.ExpectWriteStop({0x69, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6a, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4b, 0x01}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4c, 0x01}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6e, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6f, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6b, 0x00}, ZX_OK);               // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWriteStop({0x6c, 0x00}, ZX_OK);               // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0x39, 0x06}, ZX_OK);               // Input mic gain.
  mock_i2c_.ExpectWriteStop({0x63, 0x80}, ZX_OK);               // Input mic control.
  mock_i2c_.ExpectWriteStop({0x33, 0x01}, ZX_OK);               // Input mic select.
  mock_i2c_.ExpectWriteStop({0x65, 0x88}, ZX_OK);               // Input mixin control.
  mock_i2c_.ExpectWriteStop({0x67, 0x80}, ZX_OK);               // Input ADC control.
  mock_i2c_.ExpectWriteStop({0x2a, 0x00}, ZX_OK);               // Input Digital routing.
  mock_i2c_.ExpectWriteStop({0xc6, 0xd7}, ZX_OK);               // Enable AAD.
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
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Unplug detected from irq trigger, jack removed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  [[maybe_unused]] auto unused = codec_->Reset();

  // Initial Watch gets status from Reset.
  auto initial_plugged_state = codec_->WatchPlugState();
  ASSERT_TRUE(initial_plugged_state.ok());
  ASSERT_FALSE(initial_plugged_state.value().plug_state.plugged());
  ASSERT_GT(initial_plugged_state.value().plug_state.plug_state_time(), 0);

  // Trigger IRQ and Watch for plugging the headset.
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));
  auto plugged_state = codec_->WatchPlugState();
  ASSERT_TRUE(plugged_state.ok());
  ASSERT_TRUE(plugged_state.value().plug_state.plugged());
  ASSERT_GT(plugged_state.value().plug_state.plug_state_time(), 0);

  // Trigger Watch and IRQ for unplugging the headset.
  auto thread = std::thread([&] {
    auto plugged_state = codec_->WatchPlugState();
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
  auto info = codec_->GetInfo();
  EXPECT_EQ(std::string(info->info.product_name.data()).compare("DA7219"), 0);
}

TEST_F(Da7219Test, PlugDetectInitiallyPlugged) {
  // Reset.
  mock_i2c_.ExpectWriteStop({0xfd, 0x01}, ZX_OK);               // Enable.
  mock_i2c_.ExpectWriteStop({0x20, 0x8c}, ZX_OK);               // PLL.
  mock_i2c_.ExpectWriteStop({0x47, 0xa0}, ZX_OK);               // Charge Pump enablement.
  mock_i2c_.ExpectWriteStop({0x69, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6a, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4b, 0x01}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x4c, 0x01}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6e, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6f, 0x80}, ZX_OK);               // HP Routing.
  mock_i2c_.ExpectWriteStop({0x6b, 0x00}, ZX_OK);               // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWriteStop({0x6c, 0x00}, ZX_OK);               // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0x39, 0x06}, ZX_OK);               // Input mic gain.
  mock_i2c_.ExpectWriteStop({0x63, 0x80}, ZX_OK);               // Input mic control.
  mock_i2c_.ExpectWriteStop({0x33, 0x01}, ZX_OK);               // Input mic select.
  mock_i2c_.ExpectWriteStop({0x65, 0x88}, ZX_OK);               // Input mixin control.
  mock_i2c_.ExpectWriteStop({0x67, 0x80}, ZX_OK);               // Input ADC control.
  mock_i2c_.ExpectWriteStop({0x2a, 0x00}, ZX_OK);               // Input Digital routing.
  mock_i2c_.ExpectWriteStop({0xc6, 0xd7}, ZX_OK);               // Enable AAD.
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
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Unplug detected from irq trigger, jack removed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Plug detected from irq trigger, jack detect completed again.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x04}, ZX_OK);
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  [[maybe_unused]] auto unused = codec_->Reset();

  // Initial Watch gets status from Reset.
  auto initial_plugged_state = codec_->WatchPlugState();
  ASSERT_TRUE(initial_plugged_state.ok());
  ASSERT_TRUE(initial_plugged_state.value().plug_state.plugged());
  ASSERT_GT(initial_plugged_state.value().plug_state.plug_state_time(), 0);

  // Trigger IRQ for a still plugged headset so we can't Watch (there would be no reply).
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));

  // Trigger Watch and IRQ for unplugging the headset.
  auto thread = std::thread([&] {
    auto plugged_state = codec_->WatchPlugState();
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
    auto plugged_state = codec_->WatchPlugState();
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
  auto info = codec_->GetInfo();
  EXPECT_EQ(std::string(info->info.product_name.data()).compare("DA7219"), 0);
}

TEST_F(Da7219Test, PlugDetectNoMicrophoneWatchBeforeReset) {
  // Unplug detected from irq trigger, jack removed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Plug detected from irq trigger, jack detect completed.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x04}, ZX_OK);
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x00}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  auto input_codec_connector_endpoints =
      fidl::CreateEndpoints<fuchsia_hardware_audio::CodecConnector>();
  EXPECT_TRUE(input_codec_connector_endpoints.is_ok());
  auto input_codec_connector =
      fidl::WireSyncClient(std::move(input_codec_connector_endpoints->client));
  auto input_device = std::make_unique<Driver>(fake_root_.get(), core_, true);
  fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::CodecConnector>>(
      core_->dispatcher(), std::move(input_codec_connector_endpoints->server), input_device.get());

  auto input_codec_endpoints = fidl::CreateEndpoints<fuchsia_hardware_audio::Codec>();
  EXPECT_TRUE(input_codec_endpoints.is_ok());
  fidl::WireSyncClient input_codec{std::move(input_codec_endpoints->client)};

  [[maybe_unused]] auto unused =
      input_codec_connector->Connect(std::move(input_codec_endpoints->server));

  ASSERT_OK(input_device->DdkAdd(ddk::DeviceAddArgs("DA7219-input")));
  input_device.release();

  // When a Watch is issued before Reset the driver has no choice but to reply with some default
  // initialized values (in this case unpluged at time "0").
  auto output_initial_state = codec_->WatchPlugState();
  ASSERT_TRUE(output_initial_state.ok());
  ASSERT_FALSE(output_initial_state.value().plug_state.plugged());
  ASSERT_EQ(output_initial_state.value().plug_state.plug_state_time(), 0);
  auto input_initial_state = input_codec->WatchPlugState();
  ASSERT_TRUE(input_initial_state.ok());
  ASSERT_FALSE(input_initial_state.value().plug_state.plugged());
  ASSERT_EQ(input_initial_state.value().plug_state.plug_state_time(), 0);

  // Trigger IRQ for unplugging the headset.
  // No additional watch reply triggered since it is the same the initial plugged state.
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));

  // Trigger IRQ and Watch for plugging the headset.
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));

  auto output_state = codec_->WatchPlugState();
  ASSERT_TRUE(output_state.ok());
  ASSERT_TRUE(output_state.value().plug_state.plugged());
  auto input_state = input_codec->WatchPlugState();
  ASSERT_TRUE(input_state.ok());
  ASSERT_FALSE(input_state.value().plug_state.plugged());  // No mic reports unplugged.
  // The last 2-way sync call makes sure the IRQ processing is completed in the server.
}

TEST_F(Da7219Test, PlugDetectWithMicrophoneWatchBeforeReset) {
  // Unplug detected from irq trigger, jack removed with mic.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6b, 0x77}, ZX_OK);  // HP Routing (Left HP disabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0xff}, ZX_OK)
      .ExpectWriteStop({0x6c, 0x77}, ZX_OK);       // HP Routing (Right HP disabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  // Plug detected from irq trigger, jack detect completed with mic.
  mock_i2c_.ExpectWrite({0xc2}).ExpectReadStop({0x04}, ZX_OK);
  mock_i2c_.ExpectWrite({0xc0}).ExpectReadStop({0x02}, ZX_OK);
  mock_i2c_.ExpectWrite({0x6b})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6b, 0xFF}, ZX_OK);  // HP Routing (Left HP enabled).
  mock_i2c_.ExpectWrite({0x6c})
      .ExpectReadStop({0x77}, ZX_OK)
      .ExpectWriteStop({0x6c, 0xff}, ZX_OK);       // HP Routing (Right HP enabled).
  mock_i2c_.ExpectWriteStop({0xc2, 0x07}, ZX_OK);  // Clear all.

  auto input_codec_connector_endpoints =
      fidl::CreateEndpoints<fuchsia_hardware_audio::CodecConnector>();
  EXPECT_TRUE(input_codec_connector_endpoints.is_ok());
  auto input_codec_connector =
      fidl::WireSyncClient(std::move(input_codec_connector_endpoints->client));
  auto input_device = std::make_unique<Driver>(fake_root_.get(), core_, true);
  fidl::BindServer<fidl::WireServer<fuchsia_hardware_audio::CodecConnector>>(
      core_->dispatcher(), std::move(input_codec_connector_endpoints->server), input_device.get());

  auto input_codec_endpoints = fidl::CreateEndpoints<fuchsia_hardware_audio::Codec>();
  EXPECT_TRUE(input_codec_endpoints.is_ok());
  fidl::WireSyncClient input_codec{std::move(input_codec_endpoints->client)};

  [[maybe_unused]] auto unused =
      input_codec_connector->Connect(std::move(input_codec_endpoints->server));

  ASSERT_OK(input_device->DdkAdd(ddk::DeviceAddArgs("DA7219-input")));
  input_device.release();

  // When a Watch is issued before Reset the driver has no choice but to reply with some default
  // initialized values (in this case unpluged at time "0").
  auto output_initial_state = codec_->WatchPlugState();
  ASSERT_TRUE(output_initial_state.ok());
  ASSERT_FALSE(output_initial_state.value().plug_state.plugged());
  ASSERT_EQ(output_initial_state.value().plug_state.plug_state_time(), 0);
  auto input_initial_state = input_codec->WatchPlugState();
  ASSERT_TRUE(input_initial_state.ok());
  ASSERT_FALSE(input_initial_state.value().plug_state.plugged());
  ASSERT_EQ(input_initial_state.value().plug_state.plug_state_time(), 0);

  // Trigger IRQ for unplugging the headset.
  // No additional watch reply triggered since it is the same the initial plugged state.
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));

  // Trigger IRQ and Watch for plugging the headset.
  ASSERT_OK(irq().trigger(0, zx::clock::get_monotonic()));

  auto output_state = codec_->WatchPlugState();
  ASSERT_TRUE(output_state.ok());
  ASSERT_TRUE(output_state.value().plug_state.plugged());
  auto input_state = input_codec->WatchPlugState();
  ASSERT_TRUE(input_state.ok());
  ASSERT_TRUE(input_state.value().plug_state.plugged());  // With mic reports plugged.
  // The last 2-way sync call makes sure the IRQ processing is completed in the server.
}
}  // namespace audio::da7219
