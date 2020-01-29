// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-unit-test/utils.h>
#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

#include "ot_radio.h"

namespace {

TEST(OtRadioTestCase, InitTest) {
  std::unique_ptr<ot::OtRadioDevice> dev;
  // Init device
  ASSERT_OK(ot::OtRadioDevice::Create(nullptr, driver_unit_test::GetParent(), &dev));
  // Starts the thread
  ASSERT_OK(dev->Start());
  // Trigger a reset so the radio send us something
  ASSERT_OK(dev->Reset());
  // Wait for interrupt to fire and for SPI transaction to complete
  ASSERT_OK(sync_completion_wait_deadline(&dev->spi_rx_complete_, ZX_TIME_INFINITE));
  // Verify that a valid byte was sent by the radio
  ASSERT_NE(dev->spi_rx_buffer_[0], 0);
  // Teardown
  ASSERT_OK(dev->ShutDown());
}

TEST(OtRadioTestCase, SpinelFramerTest) {
  std::unique_ptr<ot::OtRadioDevice> dev;
  // Init device
  ASSERT_OK(ot::OtRadioDevice::Create(nullptr, driver_unit_test::GetParent(), &dev));
  // Starts the thread
  ASSERT_OK(dev->Start());
  // Send get version command
  ASSERT_OK(dev->GetNCPVersion());
  // Wait for interrupt to fire and for SPI transaction to complete
  ASSERT_OK(sync_completion_wait_deadline(&dev->spi_rx_complete_, ZX_TIME_INFINITE));
  // Verify that a valid version response containing the string 'OPENTHREAD' is received
  uint8_t version_openthread_ascii[] = {0x4f, 0x50, 0x45, 0x4E, 0x54, 0x48, 0x52, 0x45, 0x41, 0x44};
  ASSERT_EQ(
      memcmp(version_openthread_ascii, &dev->spi_rx_buffer_[3], sizeof(version_openthread_ascii)),
      0);
  // Teardown
  ASSERT_OK(dev->ShutDown());
}

}  // namespace
