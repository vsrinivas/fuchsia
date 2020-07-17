// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas27xx.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <ddk/debug.h>
#include <ddk/protocol/i2c.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

namespace audio {
namespace astro {

// static
std::unique_ptr<Tas27xx> Tas27xx::Create(ddk::I2cChannel&& i2c, ddk::GpioProtocolClient&& ena_gpio,
                                         ddk::GpioProtocolClient&& fault_gpio, bool vsense,
                                         bool isense) {
  // Disable the codec
  ena_gpio.Write(0);

  fbl::AllocChecker ac;
  auto codec = std::unique_ptr<Tas27xx>(new (&ac) Tas27xx(std::move(i2c), std::move(ena_gpio),
                                                          std::move(fault_gpio), vsense, isense));
  if (!ac.check()) {
    return nullptr;
  }
  return codec;
}

zx_status_t Tas27xx::SWReset() {
  zx_status_t status = WriteReg(SW_RESET, 0x01);
  DelayMs(2);
  return status;
}

int Tas27xx::Thread() {
  zx::time timestamp;
  zx_status_t status;
  uint8_t ltch0, ltch1, ltch2;

  while (true) {
    status = irq_.wait(&timestamp);
    if (!running_.load()) {
      break;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "tas27xx: Interrupt Errror - %d\n", status);
    }
    ReadReg(INT_LTCH0, &ltch0);
    ReadReg(INT_LTCH1, &ltch1);
    ReadReg(INT_LTCH2, &ltch2);
    // Clock error interrupts may happen during a rate change as the codec
    // attempts to auto configure to the tdm bus.
    if (ltch0 & INT_MASK0_TDM_CLOCK_ERROR) {
      zxlogf(INFO, "tas27xx: TDM clock disrupted (may be due to rate change)\n");
    }
    // While these are logged as errors, the amp will enter a shutdown mode
    //  until the condition is remedied, then the output will ramp back on.
    if (ltch0 & INT_MASK0_OVER_CURRENT_ERROR) {
      zxlogf(ERROR, "tas27xx: Over current error\n");
    }
    if (ltch0 & INT_MASK0_OVER_TEMP_ERROR) {
      zxlogf(ERROR, "tas27xx: Over temperature error\n");
    }
  }

  zxlogf(INFO, "tas27xx: Exiting interrupt thread\n");
  return ZX_OK;
}

zx_status_t Tas27xx::GetTemperature(float* temperature) {
  uint8_t reg;
  zx_status_t status = ReadReg(TEMP_MSB, &reg);
  if (status != ZX_OK) {
    return status;
  }
  // Slope and offset from TAS2770 Datasheet
  *temperature = static_cast<float>(-93.0 + (reg << 4) * 0.0625);
  status = ReadReg(TEMP_LSB, &reg);
  if (status != ZX_OK) {
    return status;
  }
  *temperature = *temperature + static_cast<float>((reg >> 4) * 0.0625);
  return status;
}

zx_status_t Tas27xx::GetVbat(float* voltage) {
  uint8_t reg;
  zx_status_t status = ReadReg(VBAT_MSB, &reg);
  if (status != ZX_OK) {
    return status;
  }
  // Slope and offset from TAS2770 Datasheet
  *voltage = static_cast<float>((reg << 4) * 0.0039);
  status = ReadReg(VBAT_LSB, &reg);
  if (status != ZX_OK) {
    return status;
  }
  *voltage = *voltage + static_cast<float>((reg >> 4) * 0.0039);
  return status;
}

// Puts in active, but muted/unmuted state (clocks must be active or TDM error will trigger)
// Sets I and V sense features to proper state
zx_status_t Tas27xx::Mute(bool mute) {
  return WriteReg(PWR_CTL, static_cast<uint8_t>(((!ena_isens_) << 3) | ((!ena_vsens_) << 2) |
                                                (static_cast<uint8_t>(mute) << 0)));
}

// Shuts down I and V sense feature.
// Puts device in a shutdown state (safe to deactivate clocks after call).
zx_status_t Tas27xx::Stop() {
  return WriteReg(PWR_CTL, (1 << 3) | (1 << 2) | (0x02 << 0));
}

// Restores I and V sense feature if previosly set.
// Puts device in a normal state (started).
zx_status_t Tas27xx::Start() {
  return WriteReg(PWR_CTL,
                  static_cast<uint8_t>(((!ena_isens_) << 3) | ((!ena_vsens_) << 2) | (0x00 << 0)));
}

zx_status_t Tas27xx::GetGain(float* gain) {
  uint8_t reg;
  zx_status_t status;
  status = ReadReg(PB_CFG2, &reg);
  if (status != ZX_OK) {
    return status;
  }
  *gain = static_cast<float>(-reg * kGainStep);
  return status;
}

zx_status_t Tas27xx::SetGain(float gain) {
  gain = std::clamp(gain, GetMinGain(), GetMaxGain());
  uint8_t gain_reg = static_cast<uint8_t>(-gain / kGainStep);

  return WriteReg(PB_CFG2, gain_reg);
}

bool Tas27xx::ValidGain(float gain) { return (gain <= kMaxGain) && (gain >= kMinGain); }

zx_status_t Tas27xx::SetRate(uint32_t rate) {
  if (rate != 48000 && rate != 96000) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // bit[5]   - rate ramp, 0=48kHz, 1=44.1kHz
  // bit[4]   - auto rate, 0=enable
  // bit[3:1] - samp rate, 3=48kHz, 4=96kHz
  // bit[0]   - fsync edge, 0 = rising edge, 1 = falling edge
  return WriteReg(
      TDM_CFG0, static_cast<uint8_t>((0 << 5) | (0 << 4) | (((rate == 96000) ? 0x04 : 0x03) << 1) |
                                     (1 << 0)));
}

zx_status_t Tas27xx::HardwareShutdown() {
  if (running_.load()) {
    running_.store(false);
    irq_.destroy();
    thrd_join(thread_, NULL);
  }
  ena_gpio_.Write(0);
  DelayMs(1);
  zxlogf(INFO, "tas27xx: Hardware Shutdown Successful\n");
  return ZX_OK;
}

zx_status_t Tas27xx::Init(uint32_t rate) {
  zx_status_t status;

  // Make it safe to re-init an already running device
  HardwareShutdown();

  // Clean up and shutdown hardware in event of error
  auto on_error = fbl::MakeAutoCall([this]() { HardwareShutdown(); });

  ena_gpio_.Write(1);
  DelayMs(1);

  status = SWReset();  // software reset, will be in software shutdown state after call
  if (status != ZX_OK) {
    return status;
  }

  status = Mute(true);
  if (status != ZX_OK) {
    return status;
  }
  // bit[5:2] - SBCLK_FS_RATIO - frame sync to sclk ratio
  //             64 for two channel i2s (32 bits per channel)
  // bit[1:0] - AUTO_CLK - 1=manual, 0=auto
  status = WriteReg(CLOCK_CFG, (SBCLK_FS_RATIO_64 << 2));
  if (status != ZX_OK) {
    return status;
  }

  // Set initial configuraton of rate
  status = SetRate(rate);
  if (status != ZX_OK) {
    return status;
  }

  // bit[5:4] - RX_SCFG, 01b = Mono, Left channel
  // bit[3:2] - RX_WLEN, 00b = 16-bits word length
  // bit[0:1] - RX_SLEN, 10b = 32-bit slot length
  status = WriteReg(TDM_CFG2, (0x01 << 4) | (0x00 << 2) | 0x02);
  if (status != ZX_OK) {
    return status;
  }

  // bit[4] - 0=transmit 0 on unusued slots
  // bit[3:1] tx offset -1 per i2s
  // bit[0]   tx_edge, 0 = clock out on falling edge of sbclk
  status = WriteReg(TDM_CFG4, (1 << 1) | (0 << 0));
  if (status != ZX_OK) {
    return status;
  }

  // bit[6] - 1 = Enable vsense transmit on sdout
  // bit[5:0] - tdm bus time slot for vsense
  //            all tx slots are 8-bits wide
  //            slot 4 will align with second i2s channel
  status = WriteReg(TDM_CFG5, (0x01 << 6) | 0x04);
  if (status != ZX_OK) {
    return status;
  }

  // bit[6] - 1 = Enable isense transmit on sdout
  // bit[5:0] - tdm bus time slot for isense
  //            all tx slots are 8-bits wide
  status = WriteReg(TDM_CFG6, (0x01 << 6) | 0x00);
  if (status != ZX_OK) {
    return status;
  }

  // Read latched interrupt registers to clear
  uint8_t temp;
  ReadReg(INT_LTCH0, &temp);
  ReadReg(INT_LTCH1, &temp);
  ReadReg(INT_LTCH2, &temp);

  // Set interrupt masks
  status = WriteReg(INT_MASK0, ~(INT_MASK0_TDM_CLOCK_ERROR | INT_MASK0_OVER_CURRENT_ERROR |
                                 INT_MASK0_OVER_TEMP_ERROR));
  if (status != ZX_OK) {
    return status;
  }

  status = WriteReg(INT_MASK1, 0xff);
  if (status != ZX_OK) {
    return status;
  }
  // Interupt on any unmasked latched interrupts
  status = WriteReg(INT_CFG, 0x01);
  if (status != ZX_OK) {
    return status;
  }

  status = fault_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &irq_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "tas27xx: Could not get codec interrupt %d\n", status);
    return status;
  }

  // Start the monitoring thread
  running_.store(true);
  auto thunk = [](void* arg) -> int { return reinterpret_cast<Tas27xx*>(arg)->Thread(); };
  int ret = thrd_create_with_name(&thread_, thunk, this, "tas27xx-thread");
  if (ret != thrd_success) {
    running_.store(false);
    irq_.destroy();
    return ZX_ERR_NO_RESOURCES;
  }

  on_error.cancel();
  return ZX_OK;
}

zx_status_t Tas27xx::ReadReg(uint8_t reg, uint8_t* value) { return i2c_.ReadSync(reg, value, 1); }

zx_status_t Tas27xx::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t write_buf[2];
  write_buf[0] = reg;
  write_buf[1] = value;
  return i2c_.WriteSync(write_buf, 2);
}
}  // namespace astro
}  // namespace audio
