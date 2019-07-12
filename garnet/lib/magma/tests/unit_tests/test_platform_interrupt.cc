// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <thread>

#include "gtest/gtest.h"
#include "helper/platform_device_helper.h"
#include "magma_util/macros.h"
#include "platform_pci_device.h"

TEST(PlatformDevice, RegisterInterrupt) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_NE(platform_device, nullptr);

  // Less than 100 interrupts should be assigned to this driver.
  auto interrupt = platform_device->RegisterInterrupt(100);
  EXPECT_EQ(nullptr, interrupt);

  uint32_t index = 0;
  interrupt = platform_device->RegisterInterrupt(index);
  ASSERT_NE(nullptr, interrupt);

  std::thread thread([interrupt_raw = interrupt.get()] {
    DLOG("waiting for interrupt");
    interrupt_raw->Wait();
    DLOG("returned from interrupt");
  });

  interrupt->Signal();

  DLOG("waiting for thread");
  thread.join();
}

TEST(PlatformPciDevice, RegisterInterrupt) {
  magma::PlatformPciDevice* platform_device = TestPlatformPciDevice::GetInstance();
  ASSERT_NE(platform_device, nullptr);

  auto interrupt = platform_device->RegisterInterrupt();
  // Interrupt may be null if no core device support.
  if (interrupt) {
    std::thread thread([interrupt_raw = interrupt.get()] {
      DLOG("waiting for interrupt");
      interrupt_raw->Wait();
      DLOG("returned from interrupt");
    });

    interrupt->Signal();

    DLOG("waiting for thread");
    thread.join();
  }
}
