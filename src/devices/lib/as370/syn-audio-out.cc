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

std::unique_ptr<SynAudioOutDevice> SynAudioOutDevice::Create(ddk::MmioBuffer mmio_avio_global,
                                                             ddk::MmioBuffer mmio_i2s,
                                                             ddk::SharedDmaProtocolClient dma) {
  fbl::AllocChecker ac;
  auto dev = std::unique_ptr<SynAudioOutDevice>(
      new (&ac) SynAudioOutDevice(std::move(mmio_avio_global), std::move(mmio_i2s), dma));
  if (!ac.check()) {
    return nullptr;
  }

  return dev;
}

SynAudioOutDevice::SynAudioOutDevice(ddk::MmioBuffer mmio_avio_global, ddk::MmioBuffer mmio_i2s,
                                     ddk::SharedDmaProtocolClient dma)
    : avio_global_(std::move(mmio_avio_global)), i2s_(std::move(mmio_i2s)), dma_(dma) {
  AIO_PRI_TSD0_PRI_CTRL::Get().ReadFrom(&i2s_).set_ENABLE(false).WriteTo(
      &i2s_);  // Disable channel 0.
  AIO_IRQENABLE::Get().ReadFrom(&i2s_).set_PRIIRQ(true).WriteTo(&i2s_);
  AIO_PRI_PRIPORT::Get().ReadFrom(&i2s_).set_ENABLE(true).WriteTo(&i2s_);
}

zx_status_t SynAudioOutDevice::Init() { return ZX_OK; }

uint32_t SynAudioOutDevice::GetRingPosition() { return dma_.GetBufferPosition(DmaId::kDmaIdMa0); }

zx_status_t SynAudioOutDevice::GetBuffer(size_t size, zx::vmo* buffer) {
  return dma_.InitializeAndGetBuffer(DmaId::kDmaIdMa0, DMA_TYPE_CYCLIC, static_cast<uint32_t>(size),
                                     buffer);
}

uint64_t SynAudioOutDevice::Start() {
  AIO_PRI_TSD0_PRI_CTRL::Get().FromValue(0).set_ENABLE(true).set_MUTE(true).WriteTo(&i2s_);

  // Set MCLK = 24.576MHz = APLL0 (196.608MHz) / 8.
  AIO_MCLKPRI_ACLK_CTRL::Get()
      .FromValue(0)
      .set_sw_sync_rst(true)
      .set_clkSel(AIO_MCLKPRI_ACLK_CTRL::kDivideBy8)
      .set_clkSwitch(true)
      .set_clk_Enable(true)
      .WriteTo(&i2s_);

  // Set BCLK = 3.072MHz = MCLK (24.576MHz) / 8.
  AIO_PRI_PRIAUD_CLKDIV::Get()
      .FromValue(0)
      .set_SETTING(AIO_PRI_PRIAUD_CLKDIV::kDivideBy8)
      .WriteTo(&i2s_);

  // Set I2S, 48K, 32 bits.  So BCLK must be 32 * 2 * 48K = 3.072MHz.
  AIO_PRI_PRIAUD_CTRL::Get()
      .FromValue(0)
      .set_TDMWSHIGH(0)
      .set_TDMMODE(false)
      .set_TFM(AIO_PRI_PRIAUD_CTRL::kI2s)
      .set_TCF(AIO_PRI_PRIAUD_CTRL::kFsyncHalfPeriodEqualsTo32BitClocks)
      .set_TDM(AIO_PRI_PRIAUD_CTRL::k32BitsPerChannel)
      .set_TLSB(false)
      .set_INVFS(false)
      .set_INVCLK(true)  // i.e. I2S.
      .set_LEFTJFY(AIO_PRI_PRIAUD_CTRL::kLeftJustify)
      .WriteTo(&i2s_);

  enabled_ = true;
  dma_.Start(DmaId::kDmaIdMa0);

  AIO_PRI_TSD0_PRI_CTRL::Get().FromValue(0).set_ENABLE(true).set_MUTE(false).WriteTo(&i2s_);
  return 0;
}

void SynAudioOutDevice::Stop() {
  AIO_PRI_TSD0_PRI_CTRL::Get().ReadFrom(&i2s_).set_MUTE(true).WriteTo(&i2s_);
  enabled_ = false;
  dma_.Stop(DmaId::kDmaIdMa0);
}

void SynAudioOutDevice::Shutdown() {
  Stop();
  AIO_PRI_PRIPORT::Get().ReadFrom(&i2s_).set_ENABLE(false).WriteTo(&i2s_);
}
