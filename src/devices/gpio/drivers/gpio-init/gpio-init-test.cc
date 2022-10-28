// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpio-init.h"

#include <fuchsia/hardware/gpio/cpp/banjo-mock.h>
#include <lib/ddk/metadata.h>

#include "src/devices/testing/mock-ddk/mock-device.h"

namespace gpio_init {

TEST(GpioInitTest, Test) {
  std::shared_ptr<zx_device> fake_root = MockDevice::FakeRootParent();

  ddk::MockGpio gpio1, gpio2, gpio3;

  fake_root->AddProtocol(ZX_PROTOCOL_GPIO, gpio1.GetProto()->ops, gpio1.GetProto()->ctx, "gpio1");
  fake_root->AddProtocol(ZX_PROTOCOL_GPIO, gpio2.GetProto()->ops, gpio2.GetProto()->ctx, "gpio2");
  fake_root->AddProtocol(ZX_PROTOCOL_GPIO, gpio3.GetProto()->ops, gpio3.GetProto()->ctx, "gpio3");

  fidl::Arena arena;

  fuchsia_hardware_gpio_init::wire::GpioInitMetadata metadata;
  metadata.steps = fidl::VectorView<fuchsia_hardware_gpio_init::wire::GpioInitStep>(arena, 10);

  metadata.steps[0].fragment_name = "gpio1";
  metadata.steps[0].options = fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena)
                                  .input_flags(fuchsia_hardware_gpio::GpioFlags::kPullDown)
                                  .output_value(1)
                                  .drive_strength_ua(4000)
                                  .Build();
  gpio1.ExpectConfigIn(ZX_OK, GPIO_PULL_DOWN)
      .ExpectConfigOut(ZX_OK, 1)
      .ExpectSetDriveStrength(ZX_OK, 4000, 4000);

  metadata.steps[1].fragment_name = "gpio2";
  metadata.steps[1].options = fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena)
                                  .input_flags(fuchsia_hardware_gpio::GpioFlags::kNoPull)
                                  .alt_function(5)
                                  .drive_strength_ua(2000)
                                  .Build();
  gpio2.ExpectConfigIn(ZX_OK, GPIO_NO_PULL)
      .ExpectSetAltFunction(ZX_OK, 5)
      .ExpectSetDriveStrength(ZX_OK, 2000, 2000);

  metadata.steps[2].fragment_name = "gpio3";
  metadata.steps[2].options =
      fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena).output_value(0).Build();
  gpio3.ExpectConfigOut(ZX_OK, 0);

  metadata.steps[3].fragment_name = "gpio3";
  metadata.steps[3].options =
      fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena).output_value(1).Build();
  gpio3.ExpectConfigOut(ZX_OK, 1);

  metadata.steps[4].fragment_name = "gpio3";
  metadata.steps[4].options = fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena)
                                  .input_flags(fuchsia_hardware_gpio::GpioFlags::kPullUp)
                                  .Build();
  gpio3.ExpectConfigIn(ZX_OK, GPIO_PULL_UP);

  metadata.steps[5].fragment_name = "gpio2";
  metadata.steps[5].options = fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena)
                                  .alt_function(0)
                                  .drive_strength_ua(1000)
                                  .Build();
  gpio2.ExpectSetAltFunction(ZX_OK, 0).ExpectSetDriveStrength(ZX_OK, 1000, 1000);

  metadata.steps[6].fragment_name = "gpio2";
  metadata.steps[6].options =
      fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena).output_value(1).Build();
  gpio2.ExpectConfigOut(ZX_OK, 1);

  metadata.steps[7].fragment_name = "gpio1";
  metadata.steps[7].options = fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena)
                                  .input_flags(fuchsia_hardware_gpio::GpioFlags::kPullUp)
                                  .alt_function(0)
                                  .drive_strength_ua(4000)
                                  .Build();
  gpio1.ExpectConfigIn(ZX_OK, GPIO_PULL_UP)
      .ExpectSetAltFunction(ZX_OK, 0)
      .ExpectSetDriveStrength(ZX_OK, 4000, 4000);

  metadata.steps[8].fragment_name = "gpio1";
  metadata.steps[8].options =
      fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena).output_value(1).Build();
  gpio1.ExpectConfigOut(ZX_OK, 1);

  metadata.steps[9].fragment_name = "gpio3";
  metadata.steps[9].options = fuchsia_hardware_gpio_init::wire::GpioInitOptions::Builder(arena)
                                  .alt_function(3)
                                  .drive_strength_ua(2000)
                                  .Build();
  gpio3.ExpectSetAltFunction(ZX_OK, 3).ExpectSetDriveStrength(ZX_OK, 2000, 2000);

  fidl::unstable::OwnedEncodedMessage<fuchsia_hardware_gpio_init::wire::GpioInitMetadata> encoded(
      fidl::internal::WireFormatVersion::kV2, &metadata);
  ASSERT_TRUE(encoded.ok());

  auto message = encoded.GetOutgoingMessage().CopyBytes();
  fake_root->SetMetadata(DEVICE_METADATA_GPIO_INIT_STEPS, message.data(), message.size());

  EXPECT_OK(GpioInit::Create(nullptr, fake_root.get()));

  EXPECT_EQ(fake_root->child_count(), 1);
  device_async_remove(fake_root->children().front().get());
  mock_ddk::ReleaseFlaggedDevices(fake_root.get());

  EXPECT_NO_FAILURES(gpio1.VerifyAndClear());
  EXPECT_NO_FAILURES(gpio2.VerifyAndClear());
  EXPECT_NO_FAILURES(gpio3.VerifyAndClear());
}

}  // namespace gpio_init
