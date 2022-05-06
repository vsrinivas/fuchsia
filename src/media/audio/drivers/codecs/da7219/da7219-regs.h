// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_REGS_H_
#define SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_REGS_H_

#include <hwreg/i2c.h>

namespace audio {

// This class adds defaults and helpers to the hwreg-i2c library.
// Since all registers read/write one byte at the time IntType is uint8_t and AddrIntSize 1.
template <typename DerivedType, uint8_t address>
struct I2cRegister : public hwreg::I2cRegisterBase<DerivedType, uint8_t, 1> {
  // Read from I2C and log errors.
  static zx::status<DerivedType> Read(fidl::ClientEnd<fuchsia_hardware_i2c::Device>& i2c) {
    auto ret = Get();
    zx_status_t status = ret.ReadFrom(i2c);
    if (status != ZX_OK) {
      zxlogf(ERROR, "I2C read reg 0x%02x error: %s", ret.reg_addr(), zx_status_get_string(status));
      return zx::error(status);
    }
    return zx::ok(ret);
  }
  // Write to I2C and log errors.
  zx_status_t Write(fidl::ClientEnd<fuchsia_hardware_i2c::Device>& i2c) {
    zx_status_t status = hwreg::I2cRegisterBase<DerivedType, uint8_t, 1>::WriteTo(i2c);
    if (status != ZX_OK) {
      zxlogf(ERROR, "I2C write reg 0x%02x error: %s",
             hwreg::I2cRegisterBase<DerivedType, uint8_t, 1>::reg_addr(),
             zx_status_get_string(status));
    }
    return status;
  }
  static DerivedType Get() { return hwreg::I2cRegisterAddr<DerivedType>(address).FromValue(0); }
};

// PLL_CTRL.
struct PllCtrl : public I2cRegister<PllCtrl, 0x20> {
  DEF_FIELD(7, 6, pll_mode);
  DEF_BIT(5, pll_mclk_sqr_en);
  DEF_FIELD(4, 2, pll_indiv);
};

// DAI_CTRL.
struct DaiCtrl : public I2cRegister<DaiCtrl, 0x2c> {
  DEF_BIT(7, dai_en);
  DEF_FIELD(5, 4, dai_ch_num);
  DEF_FIELD(3, 2, dai_word_length);
  DEF_FIELD(1, 0, dai_format);
};

// DAI_TDM_CTRL.
struct DaiTdmCtrl : public I2cRegister<DaiTdmCtrl, 0x2d> {
  DEF_BIT(7, dai_tdm_mode_en);
  DEF_BIT(6, dai_oe);
  DEF_FIELD(1, 0, dai_tdm_ch_en);
};

// CP_CTRL.
struct CpCtrl : public I2cRegister<CpCtrl, 0x47> {
  DEF_BIT(7, cp_en);
  DEF_FIELD(5, 4, cp_mchange);
};

// MIXOUT_L_SELECT.
struct MixoutLSelect : public I2cRegister<MixoutLSelect, 0x4b> {
  DEF_BIT(0, mixout_l_mix_select);
};

// MIXOUT_R_SELECT.
struct MixoutRSelect : public I2cRegister<MixoutRSelect, 0x4c> {
  DEF_BIT(0, mixout_r_mix_select);
};

// HP_L_CTRL.
struct HpLCtrl : public I2cRegister<HpLCtrl, 0x6b> {
  DEF_BIT(7, hp_l_amp_en);
  DEF_BIT(6, hp_l_amp_mute_en);
  DEF_BIT(5, hp_l_amp_ramp_en);
  DEF_BIT(4, hp_l_amp_zc_en);
  DEF_BIT(3, hp_l_amp_oe);
  DEF_BIT(2, hp_l_amp_min_gain_en);
};

// HP_R_CTRL.
struct HpRCtrl : public I2cRegister<HpRCtrl, 0x6c> {
  DEF_BIT(7, hp_r_amp_en);
  DEF_BIT(6, hp_r_amp_mute_en);
  DEF_BIT(5, hp_r_amp_ramp_en);
  DEF_BIT(4, hp_r_amp_zc_en);
  DEF_BIT(3, hp_r_amp_oe);
  DEF_BIT(2, hp_r_amp_min_gain_en);
};

// MIXOUT_L_CTRL.
struct MixoutLCtrl : public I2cRegister<MixoutLCtrl, 0x6e> {
  DEF_BIT(7, mixout_l_amp_en);
};

// MIXOUT_R_CTRL.
struct MixoutRCtrl : public I2cRegister<MixoutRCtrl, 0x6f> {
  DEF_BIT(7, mixout_r_amp_en);
};

// CHIP_ID1.
struct ChipId1 : public I2cRegister<ChipId1, 0x81> {
  DEF_FIELD(7, 0, chip_id1);
};

// CHIP_ID2.
struct ChipId2 : public I2cRegister<ChipId2, 0x82> {
  DEF_FIELD(7, 0, chip_id2);
};

// CHIP_REVISION.
struct ChipRevision : public I2cRegister<ChipRevision, 0x83> {
  DEF_FIELD(7, 4, chip_major);
  DEF_FIELD(3, 0, chip_minor);
};

// SYSTEM_ACTIVE.
struct SystemActive : public I2cRegister<SystemActive, 0xfd> {
  DEF_BIT(0, system_active);
};

}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_CODECS_DA7219_DA7219_REGS_H_
