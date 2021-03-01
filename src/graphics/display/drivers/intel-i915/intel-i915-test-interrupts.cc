// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ddk/driver.h>
#include <mmio-ptr/fake.h>
#include <zxtest/zxtest.h>

#include "intel-i915.h"
#include "interrupts.h"
#include "src/devices/pci/testing/pci_protocol_fake.h"

namespace {

// Test that interrupt initialization works with both MSI and Legacy interrupt
// modes, regardless of whether the IRQ worker thread is started.
TEST(IntelI915Display, InterruptInit) {
  constexpr uint32_t kMinimumRegCount = 0xd0000 / sizeof(uint32_t);
  bool kStartThread = false;
  i915::Controller controller(nullptr);
  std::vector<uint32_t> regs(kMinimumRegCount);
  mmio_buffer_t buffer{.vaddr = FakeMmioPtr(regs.data()),
                       .offset = 0,
                       .size = regs.size() * sizeof(uint32_t),
                       .vmo = ZX_HANDLE_INVALID};

  controller.SetMmioForTesting(ddk::MmioBuffer(buffer));

  pci::FakePciProtocol pci{};
  controller.SetPciForTesting(pci.get_protocol());

  EXPECT_EQ(ZX_ERR_INTERNAL, controller.interrupts()->Init(kStartThread));

  pci.AddLegacyInterrupt();
  EXPECT_EQ(ZX_OK, controller.interrupts()->Init(kStartThread));
  EXPECT_EQ(1, pci.GetIrqCount());
  EXPECT_EQ(PCI_IRQ_MODE_LEGACY, pci.GetIrqMode());

  pci.AddMsiInterrupt();
  EXPECT_EQ(ZX_OK, controller.interrupts()->Init(kStartThread));
  EXPECT_EQ(1, pci.GetIrqCount());
  EXPECT_EQ(PCI_IRQ_MODE_MSI, pci.GetIrqMode());

  // Unset so controller teardown doesn't crash.
  controller.ResetMmioSpaceForTesting();
}

}  // namespace
