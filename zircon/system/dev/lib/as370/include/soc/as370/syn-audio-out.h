// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_OUT_H_
#define ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_OUT_H_

#include <assert.h>
#include <threads.h>

#include <memory>
#include <utility>

#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <zircon/syscalls/port.h>

#include "syn-dhub.h"

class SynAudioOutDevice {
 public:
  // Move operators are implicitly disabled.
  SynAudioOutDevice(const SynAudioOutDevice&) = delete;
  SynAudioOutDevice& operator=(const SynAudioOutDevice&) = delete;

  static std::unique_ptr<SynAudioOutDevice> Create(ddk::MmioBuffer mmio_global,
                                                   ddk::MmioBuffer mmio_dhub,
                                                   ddk::MmioBuffer mmio_avio_global,
                                                   ddk::MmioBuffer mmio_i2s,
                                                   zx::interrupt interrupt);

  static uint32_t GetDmaGranularity() { return kDmaGranularity; }

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

  uint32_t fifo_depth() const { return kFifoDepth; }

 private:
  static constexpr uint32_t kDmaGranularity = 8192;  // in bytes.
  static constexpr uint32_t kFifoDepth = 1024;       // in bytes.

  ddk::MmioBuffer global_;
  ddk::MmioBuffer avio_global_;
  ddk::MmioBuffer i2s_;
  std::unique_ptr<SynDhub> dhub_;
  zx::port port_;
  zx::interrupt interrupt_;
  thrd_t thread_;
  bool enabled_ = false;

  SynAudioOutDevice(ddk::MmioBuffer mmio_global, ddk::MmioBuffer mmio_avio_global,
                    ddk::MmioBuffer mmio_i2s, zx::interrupt interrupt);

  zx_status_t Init(ddk::MmioBuffer mmio_dhub);
  int Thread();
};

#endif  // ZIRCON_SYSTEM_DEV_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_OUT_H_
