// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syn-dhub.h"

#include <lib/mmio/mmio.h>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/as370/as370-clk.h>
#include <soc/as370/as370-hw.h>
#include <zxtest/zxtest.h>

namespace as370 {

class SynDhubWrapper : public SynDhub {
 public:
  SynDhubWrapper(ddk_mock::MockMmioRegRegion& mmio, uint32_t dma_id)
      : SynDhub(nullptr, ddk::MmioBuffer(mmio.GetMmioBuffer())), dma_id_(dma_id) {}
  void Enable(bool enable) { SynDhub::Enable(dma_id_, enable); }
  void SetBuffer(zx_paddr_t buf, size_t len) { SynDhub::SetBuffer(dma_id_, buf, len); }
  void StartDma() { SynDhub::StartDma(dma_id_, true); }
  void Init() { SynDhub::Init(dma_id_); }

 private:
  uint32_t dma_id_;
};

TEST(SynDhubTest, ConstructForChannel0) {
  auto regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioDhubSize / 4);
  ddk_mock::MockMmioRegRegion region(regs.get(), 4, as370::kAudioDhubSize / 4);

  // Stop and clear FIFO for cmd and data.
  regs[0x1'0a04 / 4].ExpectWrite(0x0000'0000);
  regs[0x1'0a08 / 4].ExpectWrite(0x0000'0001);
  regs[0x1'0a14 / 4].ExpectWrite(0x0000'0000);
  regs[0x1'0a18 / 4].ExpectWrite(0x0000'0001);

  // Stop and configure channel.
  regs[0x1'0d18 / 4].ExpectWrite(0x0000'0000);  // Stop.
  regs[0x1'0d00 / 4].ExpectWrite(0x0000'0004);  // MTU = 2 ^ 4 x 8 = 128.

  // FIFO cmd configure and start.
  regs[0x1'0a00 / 4].ExpectWrite(0x0000'0000);  // Base = 0.
  regs[0x1'0600 / 4].ExpectWrite(0x0000'0004);  // Cell depth = 4.
  regs[0x1'0a04 / 4].ExpectWrite(0x0000'0001);  // Start.

  // FIFO data configure and start.
  regs[0x1'0a10 / 4].ExpectWrite(0x0000'0020);  // Base = 32.
  regs[0x1'0618 / 4].ExpectWrite(0x0000'003c);  // Cell depth = 60.
  regs[0x1'0a14 / 4].ExpectWrite(0x0000'0001);  // Start.

  // Channel configure and start.
  regs[0x1'0d18 / 4].ExpectWrite(0x0000'0001);  // Start.
  regs[0x1'0100 / 4].ExpectWrite(0x0000'0001);  // Cell depth = 1.

  // interrupt setup.
  regs[0x1'040c / 4].ExpectRead(0xffff'ffff).ExpectWrite(0xffff'ffff);  // Clear.
  regs[0x1'0104 / 4].ExpectWrite(0x0000'0002);                          // Enable "full" interrupt.

  SynDhubWrapper test(region, DmaId::kDmaIdMa0);
  test.Init();

  region.VerifyAll();
}

TEST(SynDhubTest, EnableChannel0) {
  auto regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioDhubSize / 4);
  ddk_mock::MockMmioRegRegion region(regs.get(), 4, as370::kAudioDhubSize / 4);

  SynDhubWrapper test(region, DmaId::kDmaIdMa0);

  regs[0x1'0a04 / 4].ExpectWrite(0x0000'0000);  // Stop FIFO cmd queue.
  regs[0x1'0d18 / 4].ExpectWrite(0x0000'0000);  // Stop channel.
  regs[0x1'0d1c / 4].ExpectWrite(0x0000'0001);  // Clear channel.
  regs[0x1'0f40 / 4].ExpectRead(0x0000'0000);   // Not busy.
  regs[0x1'0f44 / 4].ExpectRead(0x0000'0000);   // Not pending.

  // Stop and clear FIFO for cmd and data.
  regs[0x1'0a04 / 4].ExpectWrite(0x0000'0000);
  regs[0x1'0a08 / 4].ExpectWrite(0x0000'0001);
  regs[0x1'0c00 / 4].ExpectRead(0x0000'0000);  // FIFO not busy.
  regs[0x1'0a14 / 4].ExpectWrite(0x0000'0000);
  regs[0x1'0a18 / 4].ExpectWrite(0x0000'0001);
  regs[0x1'0c00 / 4].ExpectRead(0x0000'0000);  // FIFO not busy.

  regs[0x1'0d18 / 4].ExpectWrite(0x0000'0001);  // Start channel.
  regs[0x1'0a04 / 4].ExpectWrite(0x0000'0001);  // Start cmd queue.
  regs[0x1'0a14 / 4].ExpectWrite(0x0000'0001);  // Start data queue.

  // We do not check for the enable DMA register writes.

  test.Enable(true);

  region.VerifyAll();
}

TEST(SynDhubTest, EnableChannel10) {
  auto regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioDhubSize / 4);
  ddk_mock::MockMmioRegRegion region(regs.get(), 4, as370::kAudioDhubSize / 4);

  SynDhubWrapper test(region, DmaId::kDmaIdPdmW0);

  regs[0x1'0b44 / 4].ExpectWrite(0x0000'0000);  // Stop FIFO cmd queue.
  regs[0x1'0e80 / 4].ExpectWrite(0x0000'0000);  // Stop channel.
  regs[0x1'0e84 / 4].ExpectWrite(0x0000'0001);  // Clear channel.
  regs[0x1'0f40 / 4].ExpectRead(0x0000'0000);   // Not busy.
  regs[0x1'0f44 / 4].ExpectRead(0x0000'0000);   // Not pending.

  // Stop and clear FIFO for cmd and data.
  regs[0x1'0b44 / 4].ExpectWrite(0x0000'0000);
  regs[0x1'0b48 / 4].ExpectWrite(0x0000'0001);
  regs[0x1'0c00 / 4].ExpectRead(0x0000'0000);  // FIFO not busy.
  regs[0x1'0b54 / 4].ExpectWrite(0x0000'0000);
  regs[0x1'0b58 / 4].ExpectWrite(0x0000'0001);
  regs[0x1'0c00 / 4].ExpectRead(0x0000'0000);  // FIFO not busy.

  regs[0x1'0e80 / 4].ExpectWrite(0x0000'0001);  // Start channel.
  regs[0x1'0b44 / 4].ExpectWrite(0x0000'0001);  // Start cmd queue.
  regs[0x1'0b54 / 4].ExpectWrite(0x0000'0001);  // Start data queue.

  test.Enable(true);

  region.VerifyAll();
}

TEST(SynDhubTest, DisableChannel0) {
  auto regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioDhubSize / 4);
  ddk_mock::MockMmioRegRegion region(regs.get(), 4, as370::kAudioDhubSize / 4);

  SynDhubWrapper test(region, DmaId::kDmaIdMa0);

  regs[0x1'0a04 / 4].ExpectWrite(0x0000'0000);  // Stop FIFO cmd queue.
  regs[0x1'0d18 / 4].ExpectWrite(0x0000'0000);  // Stop channel.
  regs[0x1'0d1c / 4].ExpectWrite(0x0000'0001);  // Clear channel.
  regs[0x1'0f40 / 4].ExpectRead(0x0000'0000);   // Not busy.
  regs[0x1'0f44 / 4].ExpectRead(0x0000'0000);   // Not pending.

  // Stop and clear FIFO for cmd and data.
  regs[0x1'0a04 / 4].ExpectWrite(0x0000'0000);
  regs[0x1'0a08 / 4].ExpectWrite(0x0000'0001);
  regs[0x1'0c00 / 4].ExpectRead(0x0000'0000);  // FIFO not busy.
  regs[0x1'0a14 / 4].ExpectWrite(0x0000'0000);
  regs[0x1'0a18 / 4].ExpectWrite(0x0000'0001);
  regs[0x1'0c00 / 4].ExpectRead(0x0000'0000);  // FIFO not busy.

  test.Enable(false);

  region.VerifyAll();
}

TEST(SynDhubTest, StartDmaForChannel0) {
  auto regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioDhubSize / 4);
  ddk_mock::MockMmioRegRegion region(regs.get(), 4, as370::kAudioDhubSize / 4);

  SynDhubWrapper test(region, DmaId::kDmaIdMa0);
  test.Enable(true);
  constexpr uint32_t address = 0x12345678;
  test.SetBuffer(address, 0x8192);

  regs[0x1'0500 / 4].ExpectRead(0x0000'0000);   // Ptr to use.
  regs[0x0'0000 / 4].ExpectWrite(address);      // Address at the ptr location.
  regs[0x0'0004 / 4].ExpectWrite(0x1001'0040);  // Size = 60 (in MTUs).
  regs[0x1'0900 / 4].ExpectWrite(0x0000'0100);  // Push cmd id 0.

  test.StartDma();

  region.VerifyAll();
}

}  // namespace as370
