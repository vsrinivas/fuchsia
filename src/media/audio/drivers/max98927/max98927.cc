// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "max98927.h"

#include <endian.h>
#include <fuchsia/hardware/audiocodec/c/fidl.h>
#include <lib/device-protocol/i2c.h>
#include <lib/fidl-utils/bind.h>
#include <zircon/assert.h>

#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>

#include "max98927-registers.h"

namespace audio {
namespace max98927 {

uint8_t Max98927Device::ReadReg(uint16_t addr) {
  uint16_t buf = htobe16(addr);
  uint8_t val = 0;
  zx_status_t status = i2c_write_read_sync(&i2c_, &buf, sizeof(buf), &val, sizeof(val));
  if (status != ZX_OK) {
    zxlogf(ERROR, "max98927: could not read reg addr: 0x%04X  status: %d", addr, status);
    return -1;
  }

  zxlogf(TRACE, "max98927: register 0x%04x read 0x%02x", addr, val);
  return val;
}

void Max98927Device::WriteReg(uint16_t addr, uint8_t val) {
  uint8_t buf[3];
  uint16_t* p = reinterpret_cast<uint16_t*>(buf);
  *p = htobe16(addr);
  buf[2] = val;
  zx_status_t status = i2c_write_sync(&i2c_, buf, sizeof(buf));
  if (status != ZX_OK) {
    zxlogf(ERROR, "alc5514: could not write reg addr/val: 0x%04x/0x%02x  status: %d", addr, val,
           status);
  }
  zxlogf(TRACE, "max98927: register 0x%04x write 0x%02x", addr, val);
}

void Max98927Device::DumpRegs() {
  constexpr uint16_t first = INTERRUPT_RAW_1;
  constexpr uint16_t last = GLOBAL_ENABLE;

  // read all registers
  // segments followed by write data (first register)
  // the address pointer is automatically incremented after each byte read
  uint16_t buf = htobe16(first);
  uint8_t out[last];
  zx_status_t status = i2c_write_read_sync(&i2c_, &buf, sizeof(buf), out, sizeof(out));
  if (status != ZX_OK) {
    zxlogf(ERROR, "max98927: could not read regs status: %d", status);
  }

  zxlogf(INFO, "max98927: register dump");
  for (uint16_t i = 0; i < last; i++) {
    zxlogf(INFO, "    [%04x]: 0x%02x", i + 1, out[i]);
  }
}

zx_status_t Max98927Device::FidlSetEnabled(bool enable) {
  if (enable) {
    Enable();
  } else {
    Disable();
  }
  return ZX_OK;
}

zx_status_t Max98927Device::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  using Binder = fidl::Binder<Max98927Device>;
  static const fuchsia_hardware_audiocodec_Device_ops_t kOps = {
      .SetEnabled = Binder::BindMember<&Max98927Device::FidlSetEnabled>,
  };
  return fuchsia_hardware_audiocodec_Device_dispatch(this, txn, msg, &kOps);
}

void Max98927Device::DdkUnbind(ddk::UnbindTxn txn) { txn.Reply(); }

void Max98927Device::DdkRelease() { delete this; }

void Max98927Device::Test() {
  // PCM config - slave mode
  WriteReg(PCM_MASTER_MODE, 0);

  // PCM config - 48kHz 16-bits
  WriteReg(PCM_SAMPLE_RATE_SETUP_1, PCM_SAMPLE_RATE_SETUP_1_DIG_IF_SR(0x8));
  WriteReg(PCM_SAMPLE_RATE_SETUP_2,
           PCM_SAMPLE_RATE_SETUP_2_SPK_SR(0x8) | PCM_SAMPLE_RATE_SETUP_2_IVADC_SR(0x8));
  WriteReg(PCM_MODE_CFG, PCM_MODE_CFG_CHANSZ_16BITS | 0x3);
  WriteReg(PCM_CLOCK_SETUP, 0x2);

  // Enable TX channels
  WriteReg(PCM_RX_EN_A, 0x3);

  // Set speaker source to tone generator
  WriteReg(SPK_SRC_SEL, SPK_SRC_SEL_TONE_GEN);

  // Generate a tone. Must do before AMP_ENABLE.AMP_ENABLE_EN and BROWNOUT_EN.AMP_DSP_EN.
  WriteReg(TONE_GEN_DC_CFG, 0x6);  // fs/64 @ 48kHz = 750Hz

  zxlogf(INFO, "max98927: playing test tone...");

  // Enable for 2 secs. The datasheet recommends GLOBAL_ENABLE then AMP_ENABLE, but
  // the part errors when the bits are toggled in that order.
  WriteReg(AMP_ENABLE, AMP_ENABLE_EN);
  WriteReg(GLOBAL_ENABLE, GLOBAL_ENABLE_EN);

  zx_nanosleep(zx_deadline_after(ZX_SEC(2)));

  WriteReg(GLOBAL_ENABLE, 0);
  WriteReg(AMP_ENABLE, 0);

  // Disable tone generator and rx paths.
  WriteReg(TONE_GEN_DC_CFG, 0);
  WriteReg(PCM_RX_EN_A, 0);

  zxlogf(INFO, "max98927: test tone done");
}

void Max98927Device::Enable() {
  // PCM config - slave mode
  WriteReg(PCM_MASTER_MODE, 0);

  // PCM config - 48kHz 16-bits TDM0
  WriteReg(PCM_SAMPLE_RATE_SETUP_1, PCM_SAMPLE_RATE_SETUP_1_DIG_IF_SR(0x8));
  WriteReg(PCM_SAMPLE_RATE_SETUP_2,
           PCM_SAMPLE_RATE_SETUP_2_SPK_SR(0x8) | PCM_SAMPLE_RATE_SETUP_2_IVADC_SR(0x8));
  WriteReg(PCM_MODE_CFG, PCM_MODE_CFG_CHANSZ_16BITS | PCM_MODE_CFG_FORMAT_TDM0);
  WriteReg(PCM_CLOCK_SETUP, 0x6);

  // Enable TX channels
  WriteReg(PCM_RX_EN_A, 0x3);

  // Set speaker source to DAI
  WriteReg(SPK_SRC_SEL, 0);

  // The datasheet recommends GLOBAL_ENABLE then AMP_ENABLE, but
  // the part errors when the bits are toggled in that order.
  WriteReg(AMP_ENABLE, AMP_ENABLE_EN);
  WriteReg(GLOBAL_ENABLE, GLOBAL_ENABLE_EN);
}

void Max98927Device::Disable() {
  // Disable TX channels
  WriteReg(PCM_RX_EN_A, 0);

  WriteReg(GLOBAL_ENABLE, 0);
  WriteReg(AMP_ENABLE, 0);
}

zx_status_t Max98927Device::Initialize() {
  // Reset device
  WriteReg(SOFTWARE_RESET, SOFTWARE_RESET_RST);

  // Set outputs to HiZ
  WriteReg(PCM_TX_HIZ_CTRL_A, 0xFF);
  WriteReg(PCM_TX_HIZ_CTRL_B, 0xFF);

  // Default monomix output is (channel 0 + channel 1) / 2
  // Default monomix input channel 0 is PCM RX channel 0
  WriteReg(PCM_SPK_MONOMIX_A, PCM_SPK_MONOMIX_A_CFG_OUTPUT_0_1 | PCM_SPK_MONOMIX_B_CFG_CH0_SRC(0));
  // Default monomix input channel 1 is PCM RX channel 1
  WriteReg(PCM_SPK_MONOMIX_B, PCM_SPK_MONOMIX_B_CFG_CH1_SRC(1));

  // Default volume (+6dB dB)
  WriteReg(AMP_VOL_CTRL, 0x34 + 24);
  WriteReg(SPK_GAIN, SPK_GAIN_PCM(SPK_GAIN_3DB));

  // Enable DC blocking filter
  WriteReg(AMP_DSP_CFG, AMP_DSP_CFG_DCBLK_EN);

  // Enable IMON/VMON DC blocker
  WriteReg(MEAS_DSP_CFG, MEAS_DSP_CFG_I_DCBLK(MEAS_DSP_CFG_FREQ_3_7HZ) |
                             MEAS_DSP_CFG_V_DCBLK(MEAS_DSP_CFG_FREQ_3_7HZ) | MEAS_DSP_CFG_DITH_EN |
                             MEAS_DSP_CFG_I_DCBLK_EN | MEAS_DSP_CFG_V_DCBLK_EN);

  // Boost output voltage & current limit
  WriteReg(BOOST_CTRL_0, 0x1C);  // 10.00V
  WriteReg(BOOST_CTRL_1, 0x3E);  // 4.00A

  // Measurement ADC config
  WriteReg(MEAS_ADC_CFG, MEAS_ADC_CFG_CH2_EN);
  WriteReg(MEAS_ADC_BASE_DIV_MSB, 0);
  WriteReg(MEAS_ADC_BASE_DIV_LSB, 0x24);

  // Brownout level
  WriteReg(BROWNOUT_LVL4_AMP1_CTRL1, 0x06);  // -6dBFS

  // Envelope tracker configuration
  WriteReg(ENV_TRACKER_VOUT_HEADROOM, 0x08);  // 1.000V
  WriteReg(ENV_TRACKER_CTRL, ENV_TRACKER_CTRL_EN);
  WriteReg(ENV_TRACKER_BOOST_VOUT_RB, 0x10);  // 8.500V

  // TODO: figure out vmon-slot-no and imon-slot-no

  // Set interleave mode
  WriteReg(PCM_TX_CH_SRC_B, PCM_TX_CH_SRC_B_INTERLEAVE);

  return ZX_OK;
}

zx_status_t Max98927Device::Bind() {
  zx_status_t st = device_get_protocol(parent(), ZX_PROTOCOL_I2C, &i2c_);
  if (st != ZX_OK) {
    zxlogf(ERROR, "max98927: could not get I2C protocol: %d", st);
    return st;
  }

  st = Initialize();
  if (st != ZX_OK) {
    return st;
  }

  // Power on by default...
  Enable();

  return DdkAdd("max98927");
}

zx_status_t Max98927Device::Create(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  std::unique_ptr<Max98927Device> dev(new (&ac) Max98927Device(parent));
  if (!ac.check()) {
    zxlogf(ERROR, "max98927: out of memory attempting to allocate device");
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t st = dev->Bind();
  if (st == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
    return st;
  }

  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = Max98927Device::Create;
  return ops;
}();

}  // namespace max98927
}  // namespace audio

// clang-format off
ZIRCON_DRIVER_BEGIN(max98927, audio::max98927::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_I2C),
    BI_ABORT_IF(NE, BIND_ACPI_HID_0_3, 0x4d583938), // 'MX98'
    BI_MATCH_IF(EQ, BIND_ACPI_HID_4_7, 0x39323700), // '927\0'
ZIRCON_DRIVER_END(max98927)
    // clang-format on
