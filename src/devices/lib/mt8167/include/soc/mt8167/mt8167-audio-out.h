// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_OUT_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_OUT_H_

#include <assert.h>
#include <fbl/macros.h>
#include <lib/mmio/mmio.h>

#include <memory>
#include <utility>

class MtAudioOutDevice {
 public:
  enum MtI2sCh {
    I2S2,  // Primary.
    I2S4,  // Secondary.
  };

  DISALLOW_COPY_ASSIGN_AND_MOVE(MtAudioOutDevice);

  static std::unique_ptr<MtAudioOutDevice> Create(ddk::MmioBuffer mmio, MtI2sCh ch);

  // Sets the buffer/length pointers for the DMA engine,
  // must reside in the lower 32-bits of the address space.
  zx_status_t SetBuffer(zx_paddr_t buf, size_t len);

  // Returns offset of dma pointer in the ring buffer.
  uint32_t GetRingPosition();

  // Starts clocking data with data fetched from the beginning of the buffer.
  uint64_t Start();

  // Stops clocking data out (physical bus signals remain active).
  void Stop();

  // Stops clocking data and quiets output signals.
  void Shutdown();

  uint32_t fifo_depth() const { return fifo_depth_; }

 private:
  const uint32_t fifo_depth_;  // in bytes.
  ddk::MmioBuffer mmio_;

  // TODO(andresoportus): Add more configuration options.
  MtAudioOutDevice(ddk::MmioBuffer mmio, uint32_t fifo_depth)
      : fifo_depth_(fifo_depth), mmio_(std::move(mmio)) {}

  void InitRegs();
};

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_OUT_H_
