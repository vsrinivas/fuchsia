// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_IN_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_IN_H_

#include <assert.h>
#include <fbl/macros.h>
#include <lib/mmio/mmio.h>

#include <memory>
#include <utility>

class MtAudioInDevice {
 public:
  enum MtI2sCh {
    I2S6,  // TDM IN.
    I2S3,  // Primary.
    I2S1,  // Secondary. Documention some times calls I2S1 "I2S".
  };

  DISALLOW_COPY_ASSIGN_AND_MOVE(MtAudioInDevice);

  static std::unique_ptr<MtAudioInDevice> Create(ddk::MmioBuffer mmio_audio,
                                                 ddk::MmioBuffer mmio_clk, ddk::MmioBuffer mmio_pll,
                                                 MtI2sCh ch);

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
  zx_status_t SetRate(uint32_t frames_per_second);
  zx_status_t SetBitsPerSample(uint32_t bits_per_sample);

 protected:
  // Protected for unit tests.
  // TODO(andresoportus): Add more configuration options.
  MtAudioInDevice(ddk::MmioBuffer mmio_audio, ddk::MmioBuffer mmio_clk, ddk::MmioBuffer mmio_pll,
                  uint32_t fifo_depth)
      : fifo_depth_(fifo_depth),
        mmio_audio_(std::move(mmio_audio)),
        mmio_clk_(std::move(mmio_clk)),
        mmio_pll_(std::move(mmio_pll)) {}
  void InitRegs();

 private:
  const uint32_t fifo_depth_;  // in bytes.
  uint32_t frames_per_second_;
  uint32_t bits_per_sample_;
  ddk::MmioBuffer mmio_audio_;
  ddk::MmioBuffer mmio_clk_;
  ddk::MmioBuffer mmio_pll_;
};

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_AUDIO_IN_H_
