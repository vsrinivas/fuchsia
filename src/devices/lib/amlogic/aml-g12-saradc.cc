// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <soc/aml-common/aml-g12-saradc.h>

void AmlSaradcDevice::SetClock(uint32_t src, uint32_t div) {
  ao_mmio_.ModifyBits32(src << AO_SAR_CLK_SRC_POS, AO_SAR_CLK_SRC_MASK, AO_SAR_CLK_OFFS);
  ao_mmio_.ModifyBits32(div << AO_SAR_CLK_DIV_POS, AO_SAR_CLK_DIV_MASK, AO_SAR_CLK_OFFS);
}

void AmlSaradcDevice::Shutdown() {
  fbl::AutoLock lock(&lock_);
  Stop();
  Enable(false);
}

void AmlSaradcDevice::Stop() {
  // Stop Conversion
  adc_mmio_.SetBits32(REG0_SAMPLING_STOP_MASK, AO_SAR_ADC_REG0_OFFS);
  // Disable Sampling
  adc_mmio_.ClearBits32(REG0_SAMPLING_ENABLE_MASK, AO_SAR_ADC_REG0_OFFS);
}
void AmlSaradcDevice::ClkEna(bool ena) {
  if (ena) {
    ao_mmio_.SetBits32(AO_SAR_CLK_ENA_MASK, AO_SAR_CLK_OFFS);
  } else {
    ao_mmio_.ClearBits32(AO_SAR_CLK_ENA_MASK, AO_SAR_CLK_OFFS);
  }
}

void AmlSaradcDevice::Enable(bool ena) {
  if (ena) {
    // Enable bandgap reference
    adc_mmio_.SetBits32(REG11_TS_VBG_EN_MASK, AO_SAR_ADC_REG11_OFFS);
    // Set common mode vref
    adc_mmio_.ClearBits32(REG11_RSV6_MASK, AO_SAR_ADC_REG11_OFFS);
    // Select bandgap as reference
    adc_mmio_.ClearBits32(REG11_RSV5_MASK, AO_SAR_ADC_REG11_OFFS);
    // Enable the ADC
    adc_mmio_.SetBits32(REG3_ADC_EN_MASK, AO_SAR_ADC_REG3_OFFS);
    zx_nanosleep(zx_deadline_after(ZX_USEC(5)));
    // Enable clock source
    ClkEna(true);
  } else {
    // Disable clock source
    ClkEna(false);
    // Disable the ADC
    adc_mmio_.ClearBits32(REG3_ADC_EN_MASK, AO_SAR_ADC_REG3_OFFS);
  }
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
}

zx_status_t AmlSaradcDevice::GetSample(uint32_t channel, uint32_t *outval) {
  fbl::AutoLock lock(&lock_);
  // Slow clock for conversion
  ClkEna(false);
  SetClock(CLK_SRC_OSCIN, 160);
  ClkEna(true);

  // Select channel
  adc_mmio_.Write32(channel, AO_SAR_ADC_CHAN_LIST_OFFS);

  // Set analog mux (active and idle) to requested channel
  adc_mmio_.Write32(0x000c000c | (channel << 23) | (channel << 7), AO_SAR_ADC_DETECT_IDLE_SW_OFFS);

  // Enable sampling
  adc_mmio_.SetBits32(REG0_SAMPLING_ENABLE_MASK, AO_SAR_ADC_REG0_OFFS);

  // Start sampling
  adc_mmio_.SetBits32(REG0_SAMPLING_START_MASK, AO_SAR_ADC_REG0_OFFS);

  uint32_t busy;
  uint32_t count = 0;
  // Wait for busy state to clear
  do {
    zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
    busy = adc_mmio_.Read32(AO_SAR_ADC_REG0_OFFS);
    if (++count > 10000) {
      Stop();
      ClkEna(false);
      SetClock(CLK_SRC_OSCIN, 20);
      ClkEna(true);
      zxlogf(ERROR, "reg0 = %08x", busy);
      return ZX_ERR_UNAVAILABLE;
    }
  } while (busy & 0x70000000);

  uint32_t value = adc_mmio_.Read32(AO_SAR_ADC_FIFO_RD_OFFS);

  *outval = (value >> 2) & 0x3ff;
  Stop();
  ClkEna(false);
  SetClock(CLK_SRC_OSCIN, 20);
  ClkEna(true);

  return ZX_OK;
}

void AmlSaradcDevice::HwInit() {
  fbl::AutoLock lock(&lock_);

  adc_mmio_.Write32(0x84004040, AO_SAR_ADC_REG0_OFFS);

  // Set channel list to only channel zero
  adc_mmio_.Write32(0x00000000, AO_SAR_ADC_CHAN_LIST_OFFS);

  // Disable averaging modes
  adc_mmio_.Write32(0x00000000, AO_SAR_ADC_AVG_CNTL_OFFS);

  adc_mmio_.Write32(0x9388000a, AO_SAR_ADC_REG3_OFFS);

  adc_mmio_.Write32(0x010a000a, AO_SAR_ADC_DELAY_OFFS);

  adc_mmio_.Write32(0x03eb1a0c, AO_SAR_ADC_AUX_SW_OFFS);

  adc_mmio_.Write32(0x008c000c, AO_SAR_ADC_CHAN_10_SW_OFFS);

  adc_mmio_.Write32(0x000c000c, AO_SAR_ADC_DETECT_IDLE_SW_OFFS);
  // Disable ring counter (not used on g12)
  adc_mmio_.SetBits32((1 << 27), AO_SAR_ADC_REG3_OFFS);

  adc_mmio_.SetBits32(REG11_RSV1_MASK, AO_SAR_ADC_REG11_OFFS);

  adc_mmio_.Write32(0x00002000, AO_SAR_ADC_REG13_OFFS);

  // Select 24MHz oscillator / 20 = 1.2MHz
  SetClock(CLK_SRC_OSCIN, 20);
  Enable(true);
  zx_nanosleep(zx_deadline_after(ZX_USEC(10)));
}
