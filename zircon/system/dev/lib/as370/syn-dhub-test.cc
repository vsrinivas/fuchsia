// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/as370/as370-clk.h>
#include <soc/as370/as370-hw.h>
#include <soc/as370/syn-dhub.h>
#include <zxtest/zxtest.h>

class SynDhubWrapper : public SynDhub {
 public:
  SynDhubWrapper(ddk_mock::MockMmioRegRegion& mmio, uint8_t channel_id)
      : SynDhub(ddk::MmioBuffer(mmio.GetMmioBuffer()), channel_id) {}
};

TEST(SynDhubTest, ConstructForChannel0) {
  auto regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioDhubSize / 4);
  ddk_mock::MockMmioRegRegion region(regs.get(), 4, as370::kAudioDhubSize / 4);
  constexpr uint8_t channel_id = 0;

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

  // Clear interrupts.
  regs[0x1'0104 / 4].ExpectRead(0xffff'ffff).ExpectWrite(0xffff'ffff);

  SynDhubWrapper test(region, channel_id);

  region.VerifyAll();
}

TEST(SynDhubTest, EnableChannel0) {
  auto regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioDhubSize / 4);
  ddk_mock::MockMmioRegRegion region(regs.get(), 4, as370::kAudioDhubSize / 4);
  constexpr uint8_t channel_id = 0;

  SynDhubWrapper test(region, channel_id);

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

  test.Enable(true);

  region.VerifyAll();
}

TEST(SynDhubTest, DisableChannel0) {
  auto regs = std::make_unique<ddk_mock::MockMmioReg[]>(as370::kAudioDhubSize / 4);
  ddk_mock::MockMmioRegRegion region(regs.get(), 4, as370::kAudioDhubSize / 4);
  constexpr uint8_t channel_id = 0;

  SynDhubWrapper test(region, channel_id);

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
  constexpr uint8_t channel_id = 0;

  SynDhubWrapper test(region, channel_id);
  test.Enable(true);
  constexpr uint32_t address = 0x12345678;
  test.SetBuffer(address, 0x8192);

  regs[0x1'0500 / 4].ExpectRead(0x0000'0000);   // Ptr to use.
  regs[0x0'0000 / 4].ExpectWrite(address);      // Address at the ptr location.
  regs[0x0'0004 / 4].ExpectWrite(0x1001'0040);  // Size = 64 x MTU = 8192.
  regs[0x1'0900 / 4].ExpectWrite(0x0000'0100);  // Push cmd id 0.

  test.StartDma();

  region.VerifyAll();
}
