// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"

#include <zxtest/zxtest.h>

namespace {

TEST(FakePDev, Initializes) { fake_pdev::FakePDev fake; }

TEST(FakePDev, SetMmios) {
  fake_pdev::FakePDev fake;
  ddk::PDevProtocolClient pdev(fake.proto());

  // No mmios to start.
  pdev_mmio_t mmio;
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_STATUS(pdev.GetMmio(i, &mmio), ZX_ERR_OUT_OF_RANGE);
  }

  for (uint32_t i = 0; i < 10; i++) {
    fake.set_mmio(i, {
                         .offset = i,
                         .size = i * 2,
                     });
  }

  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_OK(pdev.GetMmio(i, &mmio));
    ASSERT_EQ(mmio.offset, i);
    ASSERT_EQ(mmio.size, i * 2);
  }
}

TEST(FakePDev, SetBtis) {
  fake_pdev::FakePDev fake;
  ddk::PDevProtocolClient pdev(fake.proto());

  zx::bti bti;
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_STATUS(pdev.GetBti(i, &bti), ZX_ERR_OUT_OF_RANGE);
  }

  for (uint32_t i = 0; i < 10; i++) {
    fake.set_bti(i, zx::bti());
  }

  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_OK(pdev.GetBti(i, &bti));
  }
}

TEST(FakePDev, SetInterrupts) {
  fake_pdev::FakePDev fake;
  ddk::PDevProtocolClient pdev(fake.proto());

  zx::interrupt irq;
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_STATUS(pdev.GetInterrupt(i, 0, &irq), ZX_ERR_OUT_OF_RANGE);
  }

  for (uint32_t i = 0; i < 10; i++) {
    fake.set_interrupt(i, zx::interrupt());
  }

  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_OK(pdev.GetInterrupt(i, 0, &irq));
  }
}

TEST(FakePDev, SetSmc) {
  fake_pdev::FakePDev fake;
  ddk::PDevProtocolClient pdev(fake.proto());

  zx::resource smc;
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_STATUS(pdev.GetSmc(i, &smc), ZX_ERR_OUT_OF_RANGE);
  }

  for (uint32_t i = 0; i < 10; i++) {
    fake.set_smc(i, zx::resource());
  }

  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_OK(pdev.GetSmc(i, &smc));
  }
}

TEST(FakePDev, UseFakeBti) {
  fake_pdev::FakePDev fake;
  ddk::PDevProtocolClient pdev(fake.proto());

  zx::bti bti;
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_STATUS(pdev.GetBti(i, &bti), ZX_ERR_OUT_OF_RANGE);
  }

  fake.UseFakeBti();
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_OK(pdev.GetBti(i, &bti));
    ASSERT_TRUE(bti.is_valid());
  }

  fake.UseFakeBti(false);
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_STATUS(pdev.GetBti(i, &bti), ZX_ERR_OUT_OF_RANGE);
  }
}

TEST(FakePDev, UseFakeSmc) {
  fake_pdev::FakePDev fake;
  ddk::PDevProtocolClient pdev(fake.proto());

  zx::resource smc;
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_STATUS(pdev.GetSmc(i, &smc), ZX_ERR_OUT_OF_RANGE);
  }

  fake.UseFakeSmc();
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_OK(pdev.GetSmc(i, &smc));
    ASSERT_TRUE(smc.is_valid());
  }

  fake.UseFakeSmc(false);
  for (uint32_t i = 0; i < 10; i++) {
    ASSERT_STATUS(pdev.GetSmc(i, &smc), ZX_ERR_OUT_OF_RANGE);
  }
}

TEST(FakePDev, GetBoardInfo) {
  fake_pdev::FakePDev fake;
  ddk::PDevProtocolClient pdev(fake.proto());

  pdev_board_info_t info = {};
  ASSERT_NE(pdev.GetBoardInfo(&info), ZX_OK);

  fake.set_board_info(info);
  ASSERT_OK(pdev.GetBoardInfo(&info));
}

TEST(FakePDev, GetDeviceInfo) {
  fake_pdev::FakePDev fake;
  ddk::PDevProtocolClient pdev(fake.proto());

  pdev_device_info_t info = {};
  ASSERT_NE(pdev.GetDeviceInfo(&info), ZX_OK);

  fake.set_device_info(info);
  ASSERT_OK(pdev.GetDeviceInfo(&info));
}

}  // namespace
