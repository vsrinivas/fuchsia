// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <utility>

#include <ddk/debug.h>
#include <fbl/alloc_checker.h>
#include <soc/mt8167/mt8167-audio-out.h>
#include <soc/mt8167/mt8167-audio-regs.h>

std::unique_ptr<MtAudioOutDevice> MtAudioOutDevice::Create(ddk::MmioBuffer mmio, MtI2sCh ch) {
  uint32_t fifo_depth = 36 * 1024;  // in bytes.

  // TODO(andresoportus): Support other configurations.
  if (ch != I2S2) {
    return nullptr;
  }

  fbl::AllocChecker ac;
  auto dev =
      std::unique_ptr<MtAudioOutDevice>(new (&ac) MtAudioOutDevice(std::move(mmio), fifo_depth));
  if (!ac.check()) {
    return nullptr;
  }

  dev->InitRegs();
  return dev;
}

void MtAudioOutDevice::InitRegs() {
  // Enable the AFE module.
  AFE_DAC_CON0::Get().ReadFrom(&mmio_).set_AFE_ON(1).WriteTo(&mmio_);

  // Power up the AFE module by clearing the power down bit.
  AUDIO_TOP_CON0::Get().ReadFrom(&mmio_).set_PDN_AFE(0).WriteTo(&mmio_);

  // I2S mode 48k, DL1 stereo, DL1 mode 48k.
  auto dac_con1 = AFE_DAC_CON1::Get().ReadFrom(&mmio_);
  dac_con1.set_DL1_DATA(0).set_DL1_MODE(10).WriteTo(&mmio_);

  // Disable clock gating.
  AUDIO_TOP_CON1::Get().ReadFrom(&mmio_).set_I2S2_BCLK_SW_CG(0).WriteTo(&mmio_);

  // I2S2: Enable, I2S (not EIAJ), 16/32 bits, OUT_MODE set to 48k, and TDMOUT PAD set to I2S.
  constexpr bool is_32_bits = 1;
  constexpr uint32_t out_mode_48k = 10;
  auto i2s_con1 = AFE_I2S_CON1::Get().ReadFrom(&mmio_);
  i2s_con1.set_I2S2_EN(1).set_I2S2_FMT(1).set_I2S2_WLEN(is_32_bits);
  i2s_con1.set_I2S2_OUT_MODE(out_mode_48k);
  i2s_con1.set_I2S2_TDMOUT_MUX(1);
  i2s_con1.WriteTo(&mmio_);

  // Enable path from DL1 data to I2S/DL_SRC.
  AFE_CONN1::Get().ReadFrom(&mmio_).set_I05_O03_S(1).WriteTo(&mmio_);
  AFE_CONN2::Get().ReadFrom(&mmio_).set_I06_O04_S(1).WriteTo(&mmio_);

  // Disable 24 bits.
  AFE_CONN_24BIT::Get().ReadFrom(&mmio_).set_O03_24BIT(0).WriteTo(&mmio_);
  AFE_CONN_24BIT::Get().ReadFrom(&mmio_).set_O04_24BIT(0).WriteTo(&mmio_);
}

uint32_t MtAudioOutDevice::GetRingPosition() {
  return AFE_DL1_CUR::Get().ReadFrom(&mmio_).reg_value() -
         AFE_DL1_BASE::Get().ReadFrom(&mmio_).reg_value();
}

zx_status_t MtAudioOutDevice::SetBuffer(zx_paddr_t buf, size_t len) {
  if ((buf % 16) || ((buf + len - 1) > std::numeric_limits<uint32_t>::max()) || (len < 16) ||
      (len % 16)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // End is inclusive.
  AFE_DL1_BASE::Get().FromValue(static_cast<uint32_t>(buf)).WriteTo(&mmio_);
  AFE_DL1_END::Get().FromValue(static_cast<uint32_t>(buf + len - 1)).WriteTo(&mmio_);
  return ZX_OK;
}

uint64_t MtAudioOutDevice::Start() {
  AFE_DAC_CON0::Get().ReadFrom(&mmio_).set_DL1_ON(1).WriteTo(&mmio_);
  return 0;
}

void MtAudioOutDevice::Stop() {
  AFE_DAC_CON0::Get().ReadFrom(&mmio_).set_DL1_ON(0).WriteTo(&mmio_);
}

void MtAudioOutDevice::Shutdown() {
  Stop();
  // Disable the AFE module.
  AFE_DAC_CON0::Get().ReadFrom(&mmio_).set_AFE_ON(0).WriteTo(&mmio_);
}
