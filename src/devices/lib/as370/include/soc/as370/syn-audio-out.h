// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_OUT_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_OUT_H_

#include <assert.h>
#include <fuchsia/hardware/shareddma/cpp/banjo.h>
#include <lib/mmio/mmio.h>
#include <threads.h>
#include <zircon/syscalls/port.h>

#include <memory>
#include <utility>

class SynAudioOutDevice {
 public:
  // Move operators are implicitly disabled.
  SynAudioOutDevice(const SynAudioOutDevice&) = delete;
  SynAudioOutDevice& operator=(const SynAudioOutDevice&) = delete;

  static std::unique_ptr<SynAudioOutDevice> Create(ddk::MmioBuffer mmio_global,
                                                   ddk::MmioBuffer mmio_avio_global,
                                                   ddk::MmioBuffer mmio_i2s,
                                                   ddk::SharedDmaProtocolClient dma);

  // Returns offset of dma pointer in the ring buffer.
  uint32_t GetRingPosition();

  // Starts clocking data with data fetched from the beginning of the buffer.
  uint64_t Start();

  // Stops clocking data out (physical bus signals remain active).
  void Stop();

  // Stops clocking data and quiets output signals.
  void Shutdown();

  uint32_t fifo_depth() const { return kFifoDepth; }
  zx_status_t GetBuffer(size_t size, zx::vmo* buffer);

 private:
  static constexpr uint32_t kFifoDepth = 1024;  // in bytes.

  ddk::MmioBuffer global_;
  ddk::MmioBuffer avio_global_;
  ddk::MmioBuffer i2s_;
  bool enabled_ = false;
  zx::vmo ring_buffer_;
  ddk::SharedDmaProtocolClient dma_;
  zx::vmo dma_buffer_;

  SynAudioOutDevice(ddk::MmioBuffer mmio_global, ddk::MmioBuffer mmio_avio_global,
                    ddk::MmioBuffer mmio_i2s, ddk::SharedDmaProtocolClient dma);

  zx_status_t Init();
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_OUT_H_
