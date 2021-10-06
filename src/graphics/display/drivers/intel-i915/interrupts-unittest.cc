// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "interrupts.h"

#include <lib/ddk/driver.h>
#include <lib/mmio-ptr/fake.h>

#include <vector>

#include <zxtest/zxtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace {

void NopPipeVsyncCb(registers::Pipe, zx_time_t) {}
void NopHotplugCb(registers::Ddi, bool) {}
void NopIrqCb(void*, uint32_t, uint64_t) {}

zx_status_t InitInterrupts(i915::Interrupts* i, zx_device_t* dev, pci::FakePciProtocol* pci,
                           ddk::MmioBuffer* mmio) {
  return i->Init(NopPipeVsyncCb, NopHotplugCb, dev, &pci->get_protocol(), mmio);
}

// Test interrupt initialization with both MSI and Legacy interrupt modes.
TEST(InterruptTest, Init) {
  constexpr uint32_t kMinimumRegCount = 0xd0000 / sizeof(uint32_t);
  std::vector<uint32_t> regs(kMinimumRegCount);
  ddk::MmioBuffer mmio_space({
      .vaddr = FakeMmioPtr(regs.data()),
      .offset = 0,
      .size = regs.size() * sizeof(uint32_t),
      .vmo = ZX_HANDLE_INVALID,
  });
  pci::FakePciProtocol pci;
  std::shared_ptr<MockDevice> parent = MockDevice::FakeRootParent();

  i915::Interrupts interrupts;
  EXPECT_EQ(ZX_ERR_INTERNAL, InitInterrupts(&interrupts, parent.get(), &pci, &mmio_space));

  pci.AddLegacyInterrupt();
  EXPECT_EQ(ZX_OK, InitInterrupts(&interrupts, parent.get(), &pci, &mmio_space));
  EXPECT_EQ(1, pci.GetIrqCount());
  EXPECT_EQ(PCI_IRQ_MODE_LEGACY, pci.GetIrqMode());

  pci.AddMsiInterrupt();
  EXPECT_EQ(ZX_OK, InitInterrupts(&interrupts, parent.get(), &pci, &mmio_space));
  EXPECT_EQ(1, pci.GetIrqCount());
  EXPECT_EQ(PCI_IRQ_MODE_MSI, pci.GetIrqMode());
}

TEST(InterruptTest, SetInterruptCallback) {
  i915::Interrupts interrupts;

  intel_gpu_core_interrupt_t callback = {.callback = NopIrqCb, .ctx = nullptr};
  EXPECT_EQ(ZX_OK, interrupts.SetInterruptCallback(&callback, 0 /* interrupt_mask */));

  // Setting a callback when one is already assigned should fail.
  EXPECT_EQ(ZX_ERR_ALREADY_BOUND,
            interrupts.SetInterruptCallback(&callback, 0 /* interrupt_mask */));

  // Clearing the existing callback with a null callback should fail.
  intel_gpu_core_interrupt_t null_callback = {.callback = nullptr, .ctx = nullptr};
  EXPECT_EQ(ZX_OK, interrupts.SetInterruptCallback(&null_callback, 0 /* interrupt_mask */));

  // It should be possible to set a new callback after clearing the old one.
  EXPECT_EQ(ZX_OK, interrupts.SetInterruptCallback(&callback, 0 /* interrupt_mask */));
}

}  // namespace
