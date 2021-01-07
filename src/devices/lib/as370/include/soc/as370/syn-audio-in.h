// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_IN_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_IN_H_

#include <assert.h>
#include <fuchsia/hardware/shareddma/cpp/banjo.h>
#include <lib/fzl/pinned-vmo.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <threads.h>
#include <zircon/syscalls/port.h>

#include <memory>
#include <utility>

#include <soc/as370/audio-dsp.h>

class SynAudioInDevice {
 public:
  // Change number of channels to 2 to demo recording of 2 channels that can be played back
  // in the same device (by default configured to use 2 channels for output).
  // Change number of channels to 1 to capture only one channel for instance when capturing PDM
  // using the TESTING_CAPTURE_PDM flag.
  static constexpr size_t kNumberOfChannels = 3;

  DISALLOW_COPY_ASSIGN_AND_MOVE(SynAudioInDevice);

  static std::unique_ptr<SynAudioInDevice> Create(ddk::MmioBuffer mmio_global,
                                                  ddk::MmioBuffer mmio_avio_global,
                                                  ddk::MmioBuffer mmio_i2s,
                                                  ddk::SharedDmaProtocolClient dma);

  zx_status_t GetBuffer(size_t size, zx::vmo* buffer);

  // Returns offset of dma pointer in the ring buffer.
  uint32_t GetRingPosition();

  zx_status_t Init();
  void ProcessDma(uint32_t index);

  // Starts clocking data with data fetched from the beginning of the buffer.
  uint64_t Start();

  // Stops clocking data out (physical bus signals remain active).
  void Stop();

  // Stops clocking data and quiets output signals.
  void Shutdown();

  uint32_t FifoDepth() const;

 protected:
  // We need one DMA every 2 channels.
  static constexpr uint32_t kNumberOfDmas = (kNumberOfChannels + 1) / 2;

  // TODO(andresoportus): Add more configuration options.
  SynAudioInDevice(ddk::MmioBuffer mmio_global, ddk::MmioBuffer mmio_avio_global,
                   ddk::MmioBuffer mmio_i2s,
                   ddk::SharedDmaProtocolClient dma);  // protected for unit testing.
  std::unique_ptr<CicFilter> cic_filter_;              // protected for unit testing.
  uint32_t dma_buffer_size_[kNumberOfDmas] = {};       // protected for unit testing.

 private:
  ddk::MmioBuffer global_;
  ddk::MmioBuffer avio_global_;
  ddk::MmioBuffer i2s_;  // the i2s name comes from the docs, not used as I2S here, has PDM config.
  const ddk::SharedDmaProtocolClient dma_;
  zx::port port_;
  zx::vmo ring_buffer_;
  zx::vmo dma_buffer_[kNumberOfDmas];
  thrd_t thread_;
  bool enabled_ = false;
  // clang-format off
  uint32_t  ring_buffer_size_             = 0;
  uint32_t  ring_buffer_current_          = 0;
  uintptr_t ring_buffer_base_             = 0;
  uint32_t  dma_buffer_current_[kNumberOfDmas] = {};
  uintptr_t dma_base_          [kNumberOfDmas] = {};
  // clang-format on
  uint32_t overflows_ = 0;

  int Thread();
  uint32_t PcmAmountPerTransfer() const;
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_SYN_AUDIO_IN_H_
