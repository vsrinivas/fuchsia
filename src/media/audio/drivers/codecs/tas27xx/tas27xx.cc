// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tas27xx.h"

#include <algorithm>
#include <memory>

#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/protocol/composite.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

#include "src/media/audio/drivers/codecs/tas27xx/ti_tas27xx-bind.h"

namespace audio {

static const std::vector<uint32_t> kSupportedNumberOfChannels = {2};
static const std::vector<SampleFormat> kSupportedSampleFormats = {SampleFormat::PCM_SIGNED};
static const std::vector<FrameFormat> kSupportedFrameFormats = {FrameFormat::I2S};
static const std::vector<uint32_t> kSupportedRates = {48'000, 96'000};
static const std::vector<uint8_t> kSupportedBitsPerSlot = {32};
static const std::vector<uint8_t> kSupportedBitsPerSample = {16};
static const audio::DaiSupportedFormats kSupportedDaiFormats = {
    .number_of_channels = kSupportedNumberOfChannels,
    .sample_formats = kSupportedSampleFormats,
    .frame_formats = kSupportedFrameFormats,
    .frame_rates = kSupportedRates,
    .bits_per_slot = kSupportedBitsPerSlot,
    .bits_per_sample = kSupportedBitsPerSample,
};

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
      zxlogf(ERROR, "tas27xx: Interrupt Errror - %d", status);
      return status;
    }
    ReadReg(INT_LTCH0, &ltch0);
    ReadReg(INT_LTCH1, &ltch1);
    ReadReg(INT_LTCH2, &ltch2);
    // Clock error interrupts may happen during a rate change as the codec
    // attempts to auto configure to the tdm bus.
    if (ltch0 & INT_MASK0_TDM_CLOCK_ERROR) {
      zxlogf(INFO, "tas27xx: TDM clock disrupted (may be due to rate change)");
    }
    // While these are logged as errors, the amp will enter a shutdown mode
    //  until the condition is remedied, then the output will ramp back on.
    if (ltch0 & INT_MASK0_OVER_CURRENT_ERROR) {
      zxlogf(ERROR, "tas27xx: Over current error");
    }
    if (ltch0 & INT_MASK0_OVER_TEMP_ERROR) {
      zxlogf(ERROR, "tas27xx: Over temperature error");
    }
  }

  zxlogf(INFO, "tas27xx: Exiting interrupt thread");
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

// Puts in active, but muted/unmuted state
// Sets I and V sense features to proper state
zx_status_t Tas27xx::UpdatePowerControl() {
  if (started_) {
    return WriteReg(PWR_CTL, static_cast<uint8_t>(((!ena_isens_) << 3) | ((!ena_vsens_) << 2) |
                                                  (static_cast<uint8_t>(gain_state_.muted) << 0)));
  } else {
    return WriteReg(PWR_CTL, static_cast<uint8_t>((1 << 3) | (1 << 2) | (0x01 << 0)));
  }
}

// Puts in active, but muted state (clocks must be active or TDM error will trigger)
// Sets I and V sense features to proper state
zx_status_t Tas27xx::Stop() {
  started_ = false;
  return UpdatePowerControl();
}

// Puts in active unmuted state (clocks must be active or TDM error will trigger)
// Sets I and V sense features to proper state
zx_status_t Tas27xx::Start() {
  started_ = true;
  return UpdatePowerControl();
}

GainFormat Tas27xx::GetGainFormat() {
  return {
      .min_gain = kMinGain,
      .max_gain = kMaxGain,
      .gain_step = kGainStep,
      .can_mute = true,
      .can_agc = false,
  };
}

GainState Tas27xx::GetGainState() { return gain_state_; }

void Tas27xx::SetGainState(GainState gain_state) {
  gain_state.gain = std::clamp(gain_state.gain, kMinGain, kMaxGain);
  uint8_t gain_reg = static_cast<uint8_t>(-gain_state.gain / kGainStep);
  WriteReg(PB_CFG2, gain_reg);
  if (gain_state.agc_enabled) {
    zxlogf(ERROR, "tas27xx: AGC enable not supported");
    gain_state.agc_enabled = false;
  }
  gain_state_ = gain_state;
  UpdatePowerControl();
}

bool Tas27xx::ValidGain(float gain) { return (gain <= kMaxGain) && (gain >= kMinGain); }

zx_status_t Tas27xx::SetRate(uint32_t rate) {
  if (rate != 48000 && rate != 96000) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Note: autorate is enabled below, so changine the codec rate is not strictly required.
  // bit[5]   - rate ramp, 0=48kHz, 1=44.1kHz
  // bit[4]   - auto rate, 0=enable
  // bit[3:1] - samp rate, 3=48kHz, 4=96kHz
  // bit[0]   - fsync edge, 0 = rising edge, 1 = falling edge
  return WriteReg(
      TDM_CFG0, static_cast<uint8_t>((0 << 5) | (0 << 4) | (((rate == 96000) ? 0x04 : 0x03) << 1) |
                                     (1 << 0)));
}

zx::status<DriverIds> Tas27xx::Initialize() {
  // Make it safe to re-init an already running device
  auto status = Shutdown();
  if (status != ZX_OK) {
    zxlogf(ERROR, "tas27xx: Could not shutdown %d", status);
    return zx::error(status);
  }

  // Clean up and shutdown in event of error
  auto on_error = fbl::MakeAutoCall([this]() { Shutdown(); });

  status = fault_gpio_.GetInterrupt(ZX_INTERRUPT_MODE_EDGE_LOW, &irq_);
  if (status != ZX_OK) {
    zxlogf(ERROR, "tas27xx: Could not get codec interrupt %d", status);
    return zx::error(status);
  }

  // Start the monitoring thread
  running_.store(true);
  auto thunk = [](void* arg) -> int { return reinterpret_cast<Tas27xx*>(arg)->Thread(); };
  int ret = thrd_create_with_name(&thread_, thunk, this, "tas27xx-thread");
  if (ret != thrd_success) {
    running_.store(false);
    irq_.destroy();
    return zx::error(ZX_ERR_NO_RESOURCES);
  }

  on_error.cancel();

  return zx::ok(DriverIds{
      .vendor_id = PDEV_VID_TI,
      .device_id = PDEV_DID_TI_TAS2770,
  });
}

zx_status_t Tas27xx::Reinitialize() {
  auto status = Stop();
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
  status = SetRate(kSupportedRates[0]);
  if (status != ZX_OK) {
    return status;
  }

  // bit[5:4] - RX_SCFG, 01b = Mono, Right channel
  // bit[3:2] - RX_WLEN, 00b = 16-bits word length
  // bit[0:1] - RX_SLEN, 10b = 32-bit slot length
  status = WriteReg(TDM_CFG2, (0x02 << 4) | (0x00 << 2) | 0x02);
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
  constexpr float kDefaultGainDb = -30.f;
  GainState gain_state = {.gain = kDefaultGainDb, .muted = true};
  SetGainState(std::move(gain_state));
  return ZX_OK;
}

zx_status_t Tas27xx::Reset() {
  // Will be in software shutdown state after call.
  zx_status_t status = WriteReg(SW_RESET, 0x01);
  DelayMs(2);
  if (status != ZX_OK) {
    return status;
  }
  return Reinitialize();
}

Info Tas27xx::GetInfo() {
  return {
      .unique_id = "",
      .manufacturer = "Texas Instruments",
      .product_name = "TAS2770",
  };
}

zx_status_t Tas27xx::Shutdown() {
  if (running_.load()) {
    running_.store(false);
    irq_.destroy();
    thrd_join(thread_, NULL);
  }
  return ZX_OK;
}

bool Tas27xx::IsBridgeable() { return false; }

void Tas27xx::SetBridgedMode(bool enable_bridged_mode) {
  if (enable_bridged_mode) {
    zxlogf(INFO, "tas27xx: bridged mode note supported");
  }
}

DaiSupportedFormats Tas27xx::GetDaiFormats() { return kSupportedDaiFormats; }

zx_status_t Tas27xx::SetDaiFormat(const DaiFormat& format) {
  ZX_ASSERT(format.channels_to_use_bitmask == 1);  // Use right channel.
  return SetRate(format.frame_rate);
}

zx_status_t Tas27xx::WriteReg(uint8_t reg, uint8_t value) {
  uint8_t write_buffer[2];
  write_buffer[0] = reg;
  write_buffer[1] = value;
//#define TRACE_I2C
#ifdef TRACE_I2C
  printf("Writing register 0x%02X to value 0x%02X\n", reg, value);
  auto status = i2c_.WriteSync(write_buffer, countof(write_buffer));
  if (status != ZX_OK) {
    printf("Could not I2C write %d\n", status);
    return status;
  }
  return ZX_OK;
#else
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret =
      i2c_.WriteSyncRetries(write_buffer, countof(write_buffer), kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "tas27xx: I2C write reg 0x%02X error %d, %d retries", reg, ret.status,
           ret.retries);
  }
  return ret.status;
#endif
}

zx_status_t Tas27xx::ReadReg(uint8_t reg, uint8_t* value) {
  constexpr uint8_t kNumberOfRetries = 2;
  constexpr zx::duration kRetryDelay = zx::msec(1);
  auto ret = i2c_.WriteReadSyncRetries(&reg, 1, value, 1, kNumberOfRetries, kRetryDelay);
  if (ret.status != ZX_OK) {
    zxlogf(ERROR, "tas27xx: I2C read reg 0x%02X error %d, %d retries", reg, ret.status,
           ret.retries);
  }
#ifdef TRACE_I2C
  printf("Read register 0x%02X, value %02X\n", reg, *value);
#endif
  return ret.status;
}

zx_status_t tas27xx_bind(void* ctx, zx_device_t* parent) {
  ddk::CompositeProtocolClient composite(parent);
  if (!composite.is_valid()) {
    zxlogf(ERROR, "tas27xx: Could not get composite protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  ddk::I2cChannel i2c(composite, "i2c");
  if (!i2c.is_valid()) {
    zxlogf(ERROR, "tas27xx: Could not get i2c protocol");
    return ZX_ERR_NO_RESOURCES;
  }

  ddk::GpioProtocolClient gpio(composite, "gpio");
  if (!gpio.is_valid()) {
    zxlogf(ERROR, "tas27xx: Could not get gpio protocol");
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto dev = SimpleCodecServer::Create<Tas27xx>(parent, i2c, gpio, false, false);

  // devmgr is now in charge of the memory for dev.
  dev.release();
  return ZX_OK;
}

static zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = tas27xx_bind;
  return ops;
}();

}  // namespace audio

ZIRCON_DRIVER(ti_tas27xx, audio::driver_ops, "zircon", "0.1")
