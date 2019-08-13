// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <utility>

#include <fbl/alloc_checker.h>
#include <soc/as370/as370-audio-regs.h>
#include <soc/as370/as370-clk-regs.h>
#include <soc/as370/as370-dma.h>
#include <soc/as370/syn-audio-out.h>

std::unique_ptr<SynAudioOutDevice> SynAudioOutDevice::Create(ddk::MmioBuffer mmio_global,
                                                             ddk::MmioBuffer mmio_avio_global,
                                                             ddk::MmioBuffer mmio_i2s,
                                                             ddk::SharedDmaProtocolClient dma) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<SynAudioOutDevice>(new (&ac) SynAudioOutDevice(
      std::move(mmio_global), std::move(mmio_avio_global), std::move(mmio_i2s), dma));
  if (!ac.check()) {
    return nullptr;
  }

  return dev;
}

SynAudioOutDevice::SynAudioOutDevice(ddk::MmioBuffer mmio_global, ddk::MmioBuffer mmio_avio_global,
                                     ddk::MmioBuffer mmio_i2s, ddk::SharedDmaProtocolClient dma)
    : global_(std::move(mmio_global)),
      avio_global_(std::move(mmio_avio_global)),
      i2s_(std::move(mmio_i2s)),
      dma_(dma) {
  AIO_PRI_TSD0_PRI_CTRL::Get().ReadFrom(&i2s_).set_ENABLE(0).WriteTo(&i2s_);  // Disable channel 0.
  AIO_IRQENABLE::Get().ReadFrom(&i2s_).set_PRIIRQ(1).WriteTo(&i2s_);
  AIO_PRI_PRIPORT::Get().ReadFrom(&i2s_).set_ENABLE(1).WriteTo(&i2s_);
}

zx_status_t SynAudioOutDevice::Init() { return ZX_OK; }

uint32_t SynAudioOutDevice::GetRingPosition() { return dma_.GetBufferPosition(DmaId::kDmaIdMa0); }

zx_status_t SynAudioOutDevice::GetBuffer(size_t size, zx::vmo* buffer) {
  return dma_.InitializeAndGetBuffer(DmaId::kDmaIdMa0, DMA_TYPE_CYCLIC, static_cast<uint32_t>(size),
                                     buffer);
}

uint64_t SynAudioOutDevice::Start() {
  AIO_PRI_TSD0_PRI_CTRL::Get().FromValue(0).set_ENABLE(1).set_MUTE(1).WriteTo(&i2s_);

  constexpr uint32_t divider = 4;  // BCLK = MCLK (24.576 MHz) / 8 = 3.072 MHz.
  AIO_PRI_PRIAUD_CLKDIV::Get().FromValue(0).set_SETTING(divider).WriteTo(&i2s_);

  AIO_MCLKPRI_ACLK_CTRL::Get()
      .FromValue(0)
      .set_sw_sync_rst(1)
      .set_clkSel(4)  // MCLK = APLL0 (196.608MHz) / 8 = 24.576MHz.
      .set_clkSwitch(1)
      .set_clk_Enable(1)
      .WriteTo(&i2s_);

  // Set I2S, 48K, 32 bits.  So BCLK must be 32 * 2 * 48K = 3.072MHz.
  AIO_PRI_PRIAUD_CTRL::Get()
      .FromValue(0)
      .set_LEFTJFY(0)  // left.
      .set_INVCLK(0)
      .set_INVFS(0)
      .set_TLSB(0)     // MSB first.
      .set_TDM(0)      // Channel resolution, 16 bits per channel.
      .set_TCF(2)      // 32 bit-cloks for FSYNC half-period.
      .set_TFM(2)      // I2S.
      .set_TDMMODE(0)  // I2S.
      .set_TDMWSHIGH(0)
      .WriteTo(&i2s_);

  enabled_ = true;
  dma_.Start(DmaId::kDmaIdMa0);

  AIO_PRI_TSD0_PRI_CTRL::Get().FromValue(0).set_ENABLE(1).set_MUTE(0).WriteTo(&i2s_);
  return 0;
}

void SynAudioOutDevice::Stop() {
  AIO_PRI_TSD0_PRI_CTRL::Get().ReadFrom(&i2s_).set_MUTE(1).WriteTo(&i2s_);
  enabled_ = false;
  dma_.Stop(DmaId::kDmaIdMa0);
}

void SynAudioOutDevice::Shutdown() {
  Stop();
  AIO_PRI_PRIPORT::Get().ReadFrom(&i2s_).set_ENABLE(0).WriteTo(&i2s_);
}
