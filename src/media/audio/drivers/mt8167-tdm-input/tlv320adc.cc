// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tlv320adc.h"

#include <string.h>

#include <ddk/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <lib/device-protocol/i2c.h>

namespace audio {
namespace mt8167 {

constexpr float Tlv320adc::kMaxGain;
constexpr float Tlv320adc::kMinGain;

std::unique_ptr<Tlv320adc> Tlv320adc::Create(const ddk::I2cChannel& i2c, uint32_t i2c_index) {
  fbl::AllocChecker ac;
  auto ptr = std::unique_ptr<Tlv320adc>(new (&ac) Tlv320adc(i2c));
  if (!ac.check()) {
    return nullptr;
  }
  return ptr;
}

zx_status_t Tlv320adc::Reset() { return WriteReg(0, 1, 0x01); }

zx_status_t Tlv320adc::SetGain(float gain) {
  gain = fbl::clamp(gain, kMinGain, kMaxGain);

  // TODO(andresoportus): Add fine vol control at reg 82.
  uint8_t gain_reg = static_cast<uint8_t>(gain * 2.f) & 0x7F;

  zx_status_t status;
  status = WriteReg(0, 83, gain_reg);  // Left gain.
  if (status != ZX_OK) {
    return status;
  }
  status = WriteReg(0, 84, gain_reg);  // Right gain.
  if (status != ZX_OK) {
    return status;
  }
  current_gain_ = gain;
  return status;
}

bool Tlv320adc::ValidGain(float gain) { return (gain <= kMaxGain) && (gain >= kMinGain); }

zx_status_t Tlv320adc::Init() {
  zx_status_t status = Standby();

  uint8_t defaults[][2] = {
      // Clocks.
      {4, 0x00},  // PLL_CLKIN = MCLK (device pin), CODEC_CLKIN = MCLK (device pin).
      // DMCLK (Digital Mic CLK, example range 1.45MHz to 4.8MHz) is based on MCLK, e.g.:
      // DMCLK = MCLK 22.5792 MHz (from Aud1) % NADC % MADC = ADC_MOD_CLK (2.8224 MHz).
      // DMCLK = MCLK 24.576 MHz (from Aud2) % NADC % MADC = ADC_MOD_CLK (3.072 MHz).
      // We need to keep MADC x AOSR > IADC (188 for PRB_R1)
      {18, 0x82},  // ADC NADC Clock Divider = 2, enabled.
      {19, 0x84},  // ADC MADC Clock Divider = 4, enabled.
      {20, 0x40},  // ADC AOSR = 64.

      // ADC Audio Interface.
      {27, 0x00},  // I2S, 16 bits, BCLK is input, WCLK is input, 3-stating of DOUT disabled.
      {28, 0x00},  // Data Slot Offset Programmability 1 (Ch_Offset_1).
      {37, 0x00},  // Data Slot Offset Programmability 2 (Ch_Offset_2).
      {38, 0x00},  // L+R channels enabled.

      // Pins.
      {51, 0x28},  // DMCLK/GPIO2 Control, DMCLK out = ADC_MOD_CLK out for the digital microphone.
      {52, 0x04},  // DMDIN/GPIO1 Control, DMDIN is in input mode.

      // ADC Config.
      {61, 0x01},  // ADC Processing Block Selection, PRB_R1.
      {80, 0x01},  // ADC Digital-Microphone Polarity Select.
      {83, 0x00},  // Left ADC Volume Control, 0dB.
      {84, 0x00},  // Right ADC Volume Control, 0dB
      {82, 0x00},  // ADC Fine Volume Control. Not muted, gain 0.
  };
  for (auto& i : defaults) {
    status = WriteReg(0, i[0], i[1]);
    if (status != ZX_OK) {
      return status;
    }
  }
  zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));  // To allow ADC clocks to sync, not on datasheet.
  status = WriteReg(0, 53, 0x12);                 // DOUT (OUT Pin) Control bus keeper enabled,
                                                  // output for codec interface.
  if (status != ZX_OK) {
    return status;
  }
  return ExitStandby();
}

zx_status_t Tlv320adc::Standby() {
  return WriteReg(0, 81, 0x02);  // ADC Digital.  ADC Off, use DMDIN, MIC off, no soft stepping.
}

zx_status_t Tlv320adc::ExitStandby() {
  return WriteReg(0, 81, 0xCE);  // ADC Digital.  ADC On, use DMDIN, MIC on, no soft stepping.
}

zx_status_t Tlv320adc::WriteReg(uint8_t page, uint8_t reg, uint8_t value) {
  uint8_t write_buf[2];
  write_buf[0] = 0;
  write_buf[1] = page;
  i2c_.WriteSync(write_buf, 2);
  write_buf[0] = reg;
  write_buf[1] = value;
  return i2c_.WriteSync(write_buf, 2);
}
}  // namespace mt8167
}  // namespace audio
