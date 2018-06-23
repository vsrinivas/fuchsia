// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <zircon/device/audio-codec.h>
#include <zircon/device/i2c.h>
#include <zircon/assert.h>

#include <fbl/alloc_checker.h>

#include "max98927.h"
#include "max98927-registers.h"

namespace audio {
namespace max98927 {

uint8_t Max98927Device::ReadReg(uint16_t addr) {
    uint8_t val = 0;

    // segments followed by write data (address)
    struct {
        i2c_slave_ioctl_segment_t segs[3];
        uint16_t addr;
    } __PACKED msg;
    msg.segs[0].type = I2C_SEGMENT_TYPE_WRITE;
    msg.segs[0].len = sizeof(addr);
    msg.segs[1].type = I2C_SEGMENT_TYPE_READ;
    msg.segs[1].len = sizeof(val);
    msg.segs[2].type = I2C_SEGMENT_TYPE_END;
    msg.segs[2].len = 0;
    msg.addr = htobe16(addr);

    size_t actual = 0;
    zx_status_t st = device_ioctl(parent(), IOCTL_I2C_SLAVE_TRANSFER, &msg, sizeof(msg),
                                  &val, sizeof(val), &actual);
    if (st != ZX_OK) {
        zxlogf(ERROR, "max98927: register 0x%04x read failed (err %d)\n", addr, st);
        return 0;
    }
    if (actual != sizeof(val)) {
        zxlogf(ERROR, "max98927: register 0x%04x read unexpected length (got %zu, expected %zu)\n",
                      addr, actual, sizeof(val));
        return 0;
    }

    zxlogf(SPEW, "max98927: register 0x%04x read 0x%02x\n", addr, val);

    return val;
}

void Max98927Device::WriteReg(uint16_t addr, uint8_t val) {
    // segments followed by write data (address and val)
    struct {
        i2c_slave_ioctl_segment_t segs[2];
        uint16_t addr;
        uint8_t val;
    } __PACKED msg;
    msg.segs[0].type = I2C_SEGMENT_TYPE_WRITE;
    msg.segs[0].len = sizeof(addr) + sizeof(val);
    msg.segs[1].type = I2C_SEGMENT_TYPE_END;
    msg.segs[1].len = 0;
    msg.addr = htobe16(addr);
    msg.val = val;

    size_t actual = 0;
    zx_status_t st = device_ioctl(parent(), IOCTL_I2C_SLAVE_TRANSFER, &msg, sizeof(msg),
                                  NULL, 0, &actual);
    if (st != ZX_OK) {
        zxlogf(ERROR, "max98927: register 0x%04x write failed (err %d)\n", addr, st);
    }

    zxlogf(SPEW, "max98927: register 0x%04x write0x%02x\n", addr, val);
}

void Max98927Device::DumpRegs() {
    constexpr uint16_t first = INTERRUPT_RAW_1;
    constexpr uint16_t last = GLOBAL_ENABLE;

    uint8_t data[last]; // 1-based
    // read all registers
    // segments followed by write data (first register)
    // the address pointer is automatically incremented after each byte read
    struct {
        i2c_slave_ioctl_segment_t segs[3];
        uint16_t addr;
    } __PACKED msg;
    msg.segs[0].type = I2C_SEGMENT_TYPE_WRITE;
    msg.segs[0].len = sizeof(uint16_t);
    msg.segs[1].type = I2C_SEGMENT_TYPE_READ;
    msg.segs[1].len = sizeof(uint8_t) * last;
    msg.segs[2].type = I2C_SEGMENT_TYPE_END;
    msg.segs[2].len = 0;
    msg.addr = htobe16(first);

    size_t actual = 0;
    zx_status_t st = device_ioctl(parent(), IOCTL_I2C_SLAVE_TRANSFER, &msg, sizeof(msg),
                                  data, sizeof(data), &actual);
    if (st != ZX_OK) {
        zxlogf(ERROR, "max98927: register dump failed (err %d)\n", st);
    }
    if (actual != sizeof(data)) {
        zxlogf(ERROR, "max98927: register dump unexpected length (got %zu, expected %zu)\n",
                      actual, sizeof(data));
    }

    zxlogf(INFO, "max98927: register dump\n");
    for (uint16_t i = 0; i < last; i++) {
        zxlogf(INFO, "    [%04x]: 0x%02x\n", i + 1, data[i]);
    }
}

zx_status_t Max98927Device::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                                     void* out_buf, size_t out_len, size_t* actual) {
    if (op != IOCTL_AUDIO_CODEC_ENABLE) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (in_len < sizeof(bool)) {
        return ZX_ERR_INVALID_ARGS;
    }
    const bool* enable = static_cast<const bool*>(in_buf);
    if (*enable) {
        Enable();
    } else {
        Disable();
    }
    return ZX_OK;
}

void Max98927Device::DdkUnbind() {
}

void Max98927Device::DdkRelease() {
    delete this;
}

void Max98927Device::Test() {
    // PCM config - slave mode
    WriteReg(PCM_MASTER_MODE, 0);

    // PCM config - 48kHz 16-bits
    WriteReg(PCM_SAMPLE_RATE_SETUP_1, PCM_SAMPLE_RATE_SETUP_1_DIG_IF_SR(0x8));
    WriteReg(PCM_SAMPLE_RATE_SETUP_2, PCM_SAMPLE_RATE_SETUP_2_SPK_SR(0x8) |
                                      PCM_SAMPLE_RATE_SETUP_2_IVADC_SR(0x8));
    WriteReg(PCM_MODE_CFG, PCM_MODE_CFG_CHANSZ_16BITS | 0x3);
    WriteReg(PCM_CLOCK_SETUP, 0x2);

    // Enable TX channels
    WriteReg(PCM_RX_EN_A, 0x3);

    // Set speaker source to tone generator
    WriteReg(SPK_SRC_SEL, SPK_SRC_SEL_TONE_GEN);

    // Generate a tone. Must do before AMP_ENABLE.AMP_ENABLE_EN and BROWNOUT_EN.AMP_DSP_EN.
    WriteReg(TONE_GEN_DC_CFG, 0x6); // fs/64 @ 48kHz = 750Hz

    zxlogf(INFO, "max98927: playing test tone...\n");

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

    zxlogf(INFO, "max98927: test tone done\n");
}

void Max98927Device::Enable() {
    // PCM config - slave mode
    WriteReg(PCM_MASTER_MODE, 0);

    // PCM config - 48kHz 16-bits TDM0
    WriteReg(PCM_SAMPLE_RATE_SETUP_1, PCM_SAMPLE_RATE_SETUP_1_DIG_IF_SR(0x8));
    WriteReg(PCM_SAMPLE_RATE_SETUP_2, PCM_SAMPLE_RATE_SETUP_2_SPK_SR(0x8) |
                                      PCM_SAMPLE_RATE_SETUP_2_IVADC_SR(0x8));
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
    WriteReg(PCM_SPK_MONOMIX_A, PCM_SPK_MONOMIX_A_CFG_OUTPUT_0_1 |
                                PCM_SPK_MONOMIX_B_CFG_CH0_SRC(0));
    // Default monomix input channel 1 is PCM RX channel 1
    WriteReg(PCM_SPK_MONOMIX_B, PCM_SPK_MONOMIX_B_CFG_CH1_SRC(1));

    // Default volume (+6dB dB)
    WriteReg(AMP_VOL_CTRL, 0x34 + 24);
    WriteReg(SPK_GAIN, SPK_GAIN_PCM(SPK_GAIN_3DB));

    // Enable DC blocking filter
    WriteReg(AMP_DSP_CFG, AMP_DSP_CFG_DCBLK_EN);

    // Enable IMON/VMON DC blocker
    WriteReg(MEAS_DSP_CFG, MEAS_DSP_CFG_I_DCBLK(MEAS_DSP_CFG_FREQ_3_7HZ) |
                           MEAS_DSP_CFG_V_DCBLK(MEAS_DSP_CFG_FREQ_3_7HZ) |
                           MEAS_DSP_CFG_DITH_EN |
                           MEAS_DSP_CFG_I_DCBLK_EN |
                           MEAS_DSP_CFG_V_DCBLK_EN);

    // Boost output voltage & current limit
    WriteReg(BOOST_CTRL_0, 0x1C); // 10.00V
    WriteReg(BOOST_CTRL_1, 0x3E); // 4.00A

    // Measurement ADC config
    WriteReg(MEAS_ADC_CFG, MEAS_ADC_CFG_CH2_EN);
    WriteReg(MEAS_ADC_BASE_DIV_MSB, 0);
    WriteReg(MEAS_ADC_BASE_DIV_LSB, 0x24);

    // Brownout level
    WriteReg(BROWNOUT_LVL4_AMP1_CTRL1, 0x06); // -6dBFS

    // Envelope tracker configuration
    WriteReg(ENV_TRACKER_VOUT_HEADROOM, 0x08); // 1.000V
    WriteReg(ENV_TRACKER_CTRL, ENV_TRACKER_CTRL_EN);
    WriteReg(ENV_TRACKER_BOOST_VOUT_RB, 0x10); // 8.500V

    // TODO: figure out vmon-slot-no and imon-slot-no

    // Set interleave mode
    WriteReg(PCM_TX_CH_SRC_B, PCM_TX_CH_SRC_B_INTERLEAVE);

    return ZX_OK;
}

zx_status_t Max98927Device::Bind() {
    zx_status_t st = Initialize();
    if (st != ZX_OK) {
        return st;
    }

    // Power on by default...
    Enable();

    return DdkAdd("max98927");
}

fbl::unique_ptr<Max98927Device> Max98927Device::Create(zx_device_t* parent) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<Max98927Device> ret(new (&ac) Max98927Device(parent));
    if (!ac.check()) {
        zxlogf(ERROR, "max98927: out of memory attempting to allocate device\n");
        return nullptr;
    }
    return ret;
}

}  // namespace max98927
}  // namespace audio

extern "C" {
zx_status_t max98927_bind_hook(void* ctx, zx_device_t* parent) {
    auto dev = audio::max98927::Max98927Device::Create(parent);
    if (dev == nullptr) {
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
}  // extern "C"
