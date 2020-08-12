// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS27XX_TAS27XX_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS27XX_TAS27XX_H_

#include <lib/device-protocol/i2c-channel.h>
#include <lib/simple-codec/simple-codec-server.h>
#include <threads.h>

#include <memory>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>

namespace audio {
static constexpr uint8_t SW_RESET = 0x01;  // sw reset
static constexpr uint8_t PWR_CTL = 0x02;   // power control
static constexpr uint8_t PB_CFG2 = 0x05;   // pcm gain register
static constexpr uint8_t TDM_CFG0 = 0x0a;
static constexpr uint8_t TDM_CFG1 = 0x0b;
static constexpr uint8_t TDM_CFG2 = 0x0c;
static constexpr uint8_t TDM_CFG3 = 0x0d;
static constexpr uint8_t TDM_CFG4 = 0x0e;
static constexpr uint8_t TDM_CFG5 = 0x0f;
static constexpr uint8_t TDM_CFG6 = 0x10;
static constexpr uint8_t TDM_CFG7 = 0x11;
static constexpr uint8_t TDM_CFG8 = 0x12;
static constexpr uint8_t TDM_CFG9 = 0x13;
static constexpr uint8_t TDM_CFG10 = 0x14;
static constexpr uint8_t INT_MASK0 = 0x20;
static constexpr uint8_t INT_MASK1 = 0x21;
static constexpr uint8_t INT_LTCH0 = 0x24;
static constexpr uint8_t INT_LTCH1 = 0x25;
static constexpr uint8_t INT_LTCH2 = 0x26;
static constexpr uint8_t VBAT_MSB = 0x27;
static constexpr uint8_t VBAT_LSB = 0x28;
static constexpr uint8_t TEMP_MSB = 0x29;
static constexpr uint8_t TEMP_LSB = 0x2a;
static constexpr uint8_t INT_CFG = 0x30;
static constexpr uint8_t MISC_IRQ = 0x32;
static constexpr uint8_t CLOCK_CFG = 0x3c;  // Clock Config

static constexpr uint8_t SBCLK_FS_RATIO_16 = 0x00;
static constexpr uint8_t SBCLK_FS_RATIO_24 = 0x01;
static constexpr uint8_t SBCLK_FS_RATIO_32 = 0x02;
static constexpr uint8_t SBCLK_FS_RATIO_48 = 0x03;
static constexpr uint8_t SBCLK_FS_RATIO_64 = 0x04;
static constexpr uint8_t SBCLK_FS_RATIO_96 = 0x05;
static constexpr uint8_t SBCLK_FS_RATIO_128 = 0x06;
static constexpr uint8_t SBCLK_FS_RATIO_192 = 0x07;
static constexpr uint8_t SBCLK_FS_RATIO_256 = 0x08;
static constexpr uint8_t SBCLK_FS_RATIO_384 = 0x09;
static constexpr uint8_t SBCLK_FS_RATIO_512 = 0x0a;

static constexpr uint8_t INT_MASK0_TDM_CLOCK_ERROR = (1 << 2);
static constexpr uint8_t INT_MASK0_OVER_CURRENT_ERROR = (1 << 1);
static constexpr uint8_t INT_MASK0_OVER_TEMP_ERROR = (1 << 0);

class Tas27xx : public SimpleCodecServer {
 public:
  explicit Tas27xx(zx_device_t* device, ddk::I2cChannel i2c, ddk::GpioProtocolClient fault_gpio,
                   bool vsense, bool isense)
      : SimpleCodecServer(device),
        i2c_(i2c),
        fault_gpio_(fault_gpio),
        ena_vsens_(vsense),
        ena_isens_(isense) {}
  virtual ~Tas27xx() = default;

  // Implementation for SimpleCodecServer.
  zx_status_t Shutdown() override;

 protected:
  zx_status_t Reinitialize();

  // Implementation for SimpleCodecServer.
  zx::status<DriverIds> Initialize() override;
  zx_status_t Reset() override;
  Info GetInfo() override;
  zx_status_t Stop() override;
  zx_status_t Start() override;
  bool IsBridgeable() override;
  void SetBridgedMode(bool enable_bridged_mode) override;
  std::vector<DaiSupportedFormats> GetDaiFormats() override;
  zx_status_t SetDaiFormat(const DaiFormat& format) override;
  GainFormat GetGainFormat() override;
  GainState GetGainState() override;
  void SetGainState(GainState state) override;
  PlugState GetPlugState() override;

  std::atomic<bool> initialized_ = false;
  std::atomic<bool> running_ = false;
  zx::interrupt irq_;
  ddk::I2cChannel i2c_;
  const ddk::GpioProtocolClient fault_gpio_;
  bool ena_vsens_ = false;
  bool ena_isens_ = false;

 private:
  static constexpr float kMaxGain = 0;
  static constexpr float kMinGain = -100.0;
  static constexpr float kGainStep = 0.5;

  zx_status_t WriteReg(uint8_t reg, uint8_t value);
  zx_status_t ReadReg(uint8_t reg, uint8_t* value);
  int Thread();
  void DelayMs(uint32_t ms) { zx_nanosleep(zx_deadline_after(ZX_MSEC(ms))); }
  zx_status_t SetRate(uint32_t rate);
  bool ValidGain(float gain);
  zx_status_t UpdatePowerControl();
  zx_status_t GetTemperature(float* temperature);
  zx_status_t GetVbat(float* voltage);

  bool started_ = false;
  GainState gain_state_ = {};
  thrd_t thread_;
};
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_TAS27XX_TAS27XX_H_
