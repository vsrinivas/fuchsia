// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <hwreg/bitfields.h>

#ifndef SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_TSENSOR_REGS_H_
#define SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_TSENSOR_REGS_H_

// clang-format off

// Register offset
#define AML_HHI_TS_CLK_CNTL     0x64 << 2
#define AML_TS_CFG_REG1         (0x1 << 2)
#define AML_TS_CFG_REG2         (0x2 << 2)
#define AML_TS_CFG_REG3         (0x3 << 2)
#define AML_TS_CFG_REG4         (0x4 << 2)
#define AML_TS_CFG_REG5         (0x5 << 2)
#define AML_TS_CFG_REG6         (0x6 << 2)
#define AML_TS_CFG_REG7         (0x7 << 2)
#define AML_TS_CFG_REG8         (0x8 << 2)
#define AML_TS_STAT0            (0x10 << 2)
#define AML_TS_STAT1            (0x11 << 2)
#define IRQ_FALL_ENABLE_SHIFT   28
#define IRQ_RISE_ENABLE_SHIFT   24
#define IRQ_FALL_STAT_CLR_SHIFT 20
#define IRQ_RISE_STAT_CLR_SHIFT 16
#define AML_RISE_THRESHOLD_IRQ  0xf
#define AML_FALL_THRESHOLD_IRQ  0xf0
#define AML_TEMP_CAL            1
#define AML_TS_TEMP_MASK        0xfff
#define AML_TS_CH_SEL           0x3 /* set 3'b011 for work */
#define AML_HHI_TS_CLK_ENABLE   0x130U /* u-boot */
#define AML_TS_VALUE_CONT       0x10
#define AML_TS_REBOOT_TIME      0xff  // TODO(fxbug.dev/62972): Reconsider this setting

//clang-format on

namespace thermal {

class TsCfgReg1 : public hwreg::RegisterBase<TsCfgReg1, uint32_t> {
public:
    DEF_BIT(31, fall_th3_irq_en);
    DEF_BIT(30, fall_th2_irq_en);
    DEF_BIT(29, fall_th1_irq_en);
    DEF_BIT(28, fall_th0_irq_en);
    DEF_BIT(27, rise_th3_irq_en);
    DEF_BIT(26, rise_th2_irq_en);
    DEF_BIT(25, rise_th1_irq_en);
    DEF_BIT(24, rise_th0_irq_en);
    DEF_BIT(23, fall_th3_irq_stat_clr);
    DEF_BIT(22, fall_th2_irq_stat_clr);
    DEF_BIT(21, fall_th1_irq_stat_clr);
    DEF_BIT(20, fall_th0_irq_stat_clr);
    DEF_BIT(19, rise_th3_irq_stat_clr);
    DEF_BIT(18, rise_th2_irq_stat_clr);
    DEF_BIT(17, rise_th1_irq_stat_clr);
    DEF_BIT(16, rise_th0_irq_stat_clr);
    DEF_BIT(15, enable_irq);
    DEF_BIT(14, fast_mode);
    DEF_BIT(13, clr_hi_temp_stat);
    DEF_BIT(12, ts_ana_rset_vbg);
    DEF_BIT(11, ts_ana_rset_sd);
    DEF_BIT(10, ts_ana_en_vcm);
    DEF_BIT(9, ts_ana_en_vbg);
    DEF_FIELD(8, 7, filter_hcic_mode);
    DEF_BIT(6, filter_ts_out_ctrl);
    DEF_BIT(5, filter_en);
    DEF_BIT(4, ts_ena_en_iptat);
    DEF_BIT(3, ts_dem_en);
    DEF_FIELD(2, 0, bipolar_bias_current_input);

    static auto Get() { return hwreg::RegisterAddr<TsCfgReg1>(AML_TS_CFG_REG1); }
};

class TsCfgReg2 : public hwreg::RegisterBase<TsCfgReg2, uint32_t> {
public:
    DEF_BIT(31, hi_temp_enable);
    DEF_BIT(30, reset_en);
    DEF_FIELD(27, 16, high_temp_times);
    DEF_FIELD(15, 0, high_temp_threshold);

    static auto Get() { return hwreg::RegisterAddr<TsCfgReg2>(AML_TS_CFG_REG2); }
};

class TsCfgReg4 : public hwreg::RegisterBase<TsCfgReg4, uint32_t> {
public:
    DEF_FIELD(23, 12, rise_th0);
    DEF_FIELD(11, 0, rise_th1);

    static auto Get() { return hwreg::RegisterAddr<TsCfgReg4>(AML_TS_CFG_REG4); }
};

class TsCfgReg5 : public hwreg::RegisterBase<TsCfgReg5, uint32_t> {
public:
    DEF_FIELD(23, 12, rise_th0);
    DEF_FIELD(11, 0, rise_th1);

    static auto Get() { return hwreg::RegisterAddr<TsCfgReg5>(AML_TS_CFG_REG5); }
};

class TsCfgReg6 : public hwreg::RegisterBase<TsCfgReg6, uint32_t> {
public:
    DEF_FIELD(23, 12, fall_th0);
    DEF_FIELD(11, 0, fall_th1);

    static auto Get() { return hwreg::RegisterAddr<TsCfgReg6>(AML_TS_CFG_REG6); }
};

class TsCfgReg7 : public hwreg::RegisterBase<TsCfgReg7, uint32_t> {
public:
    DEF_FIELD(23, 12, fall_th0);
    DEF_FIELD(11, 0, fall_th1);

    static auto Get() { return hwreg::RegisterAddr<TsCfgReg7>(AML_TS_CFG_REG7); }
};

class TsStat0 : public hwreg::RegisterBase<TsStat0, uint32_t> {
public:
    DEF_FIELD(15, 0, temperature);

    static auto Get() { return hwreg::RegisterAddr<TsStat0>(AML_TS_STAT0); }
};

class TsStat1 : public hwreg::RegisterBase<TsStat1, uint32_t> {
public:
    DEF_BIT(8, hi_temp_stat);
    DEF_BIT(7, fall_th3_irq);
    DEF_BIT(6, fall_th2_irq);
    DEF_BIT(5, fall_th1_irq);
    DEF_BIT(4, fall_th0_irq);
    DEF_BIT(3, rise_th3_irq);
    DEF_BIT(2, rise_th2_irq);
    DEF_BIT(1, rise_th1_irq);
    DEF_BIT(0, rise_th0_irq);

    static auto Get() { return hwreg::RegisterAddr<TsStat1>(AML_TS_STAT1); }
};


} // namespace thermal

#endif  // SRC_DEVICES_THERMAL_DRIVERS_AML_THERMAL_S905D2G_AML_TSENSOR_REGS_H_
