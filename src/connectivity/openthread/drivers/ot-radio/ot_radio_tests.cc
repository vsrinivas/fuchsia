// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/driver-unit-test/utils.h>
#include <lib/sync/completion.h>

#include <zxtest/zxtest.h>

#include "ot_radio.h"

namespace {

TEST(OtRadioTestCase, InitTest) {
  std::unique_ptr<ot_radio::OtRadioDevice> dev;
  // Init device
  ASSERT_OK(ot_radio::OtRadioDevice::Create(nullptr, driver_unit_test::GetParent(), &dev));
  // Starts the thread - waits for interrupt and reads SPI
  ASSERT_OK(dev->Start());
  // Trigger a reset so the radio send us something
  ASSERT_OK(dev->Reset());
  // Wait for interrupt to fire and for SPI transaction to complete
  ASSERT_OK(sync_completion_wait_deadline(&dev->spi_rx_complete, ZX_TIME_INFINITE));
  // Verify that a valid byte was sent by the radio
  ASSERT_NE(dev->spi_rx_buffer_[0], 0);
  // Teardown
  ASSERT_OK(dev->ShutDown());
  dev.release();
}

}  // namespace
