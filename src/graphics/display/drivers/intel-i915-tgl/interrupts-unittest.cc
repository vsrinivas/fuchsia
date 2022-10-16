// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/interrupts.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/ddk/driver.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/sync/completion.h>

#include <vector>

#include <gtest/gtest.h>

#include "src/devices/pci/testing/pci_protocol_fake.h"
#include "src/devices/testing/mock-ddk/mock-device.h"
#include "src/graphics/display/drivers/intel-i915-tgl/ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"

namespace i915_tgl {

namespace {

void NopPipeVsyncCb(tgl_registers::Pipe, zx_time_t) {}
void NopHotplugCb(tgl_registers::Ddi, bool) {}
void NopIrqCb(void*, uint32_t, uint64_t) {}

zx_status_t InitInterrupts(Interrupts* i, zx_device_t* dev, ddk::Pci& pci, fdf::MmioBuffer* mmio) {
  return i->Init(NopPipeVsyncCb, NopHotplugCb, dev, pci, mmio, kTestDeviceDid);
}

class InterruptTest : public testing::Test {
 public:
  InterruptTest() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  void SetUp() override {
    loop_.StartThread("pci-fidl-server-thread");
    pci_ = fake_pci_.SetUpFidlServer(loop_);
  }

  async::Loop loop_;
  ddk::Pci pci_;
  pci::FakePciProtocol fake_pci_;
};

// Test interrupt initialization with both MSI and Legacy interrupt modes.
TEST_F(InterruptTest, Init) {
  constexpr uint32_t kMinimumRegCount = 0xd0000 / sizeof(uint32_t);
  std::vector<uint32_t> regs(kMinimumRegCount);
  fdf::MmioBuffer mmio_space({
      .vaddr = FakeMmioPtr(regs.data()),
      .offset = 0,
      .size = regs.size() * sizeof(uint32_t),
      .vmo = ZX_HANDLE_INVALID,
  });
  std::shared_ptr<MockDevice> parent = MockDevice::FakeRootParent();

  Interrupts interrupts;
  EXPECT_EQ(ZX_ERR_INTERNAL, InitInterrupts(&interrupts, parent.get(), pci_, &mmio_space));

  pci::RunAsync(loop_, [&] { fake_pci_.AddLegacyInterrupt(); });
  EXPECT_EQ(ZX_OK, InitInterrupts(&interrupts, parent.get(), pci_, &mmio_space));

  pci::RunAsync(loop_, [&] {
    EXPECT_EQ(1u, fake_pci_.GetIrqCount());
    EXPECT_EQ(PCI_INTERRUPT_MODE_LEGACY, fake_pci_.GetIrqMode());
    fake_pci_.AddMsiInterrupt();
  });

  EXPECT_EQ(ZX_OK, InitInterrupts(&interrupts, parent.get(), pci_, &mmio_space));

  pci::RunAsync(loop_, [&] {
    EXPECT_EQ(1u, fake_pci_.GetIrqCount());
    EXPECT_EQ(PCI_INTERRUPT_MODE_MSI, fake_pci_.GetIrqMode());
  });
}

TEST_F(InterruptTest, SetInterruptCallback) {
  Interrupts interrupts;

  constexpr intel_gpu_core_interrupt_t callback = {.callback = NopIrqCb, .ctx = nullptr};
  const uint32_t gpu_interrupt_mask = 0;
  EXPECT_EQ(ZX_OK, interrupts.SetGpuInterruptCallback(callback, gpu_interrupt_mask));

  // Setting a callback when one is already assigned should fail.
  EXPECT_EQ(ZX_ERR_ALREADY_BOUND, interrupts.SetGpuInterruptCallback(callback, gpu_interrupt_mask));

  // Clearing the existing callback with a null callback should fail.
  constexpr intel_gpu_core_interrupt_t null_callback = {.callback = nullptr, .ctx = nullptr};
  EXPECT_EQ(ZX_OK, interrupts.SetGpuInterruptCallback(null_callback, gpu_interrupt_mask));

  // It should be possible to set a new callback after clearing the old one.
  EXPECT_EQ(ZX_OK, interrupts.SetGpuInterruptCallback(callback, gpu_interrupt_mask));
}

}  // namespace

}  // namespace i915_tgl
