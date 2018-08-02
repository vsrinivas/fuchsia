// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#define BIT(x)                              (1UL << (x))
#define MCELSIUS                            1000
#define AML_TS_VALUE_CONT                   0x10

// Register offset
#define AML_TRIM_INFO                       0x268
#define AML_HHI_TS_CLK_CNTL                 0x64 << 2
#define AML_TS_CFG_REG1                     0x800 + (0x1  << 2 )
#define AML_TS_CFG_REG2                     0x800 + (0x2  << 2 )
#define AML_TS_CFG_REG3                     0x800 + (0x3  << 2 )
#define AML_TS_CFG_REG4                     0x800 + (0x4  << 2 )
#define AML_TS_CFG_REG5                     0x800 + (0x5  << 2 )
#define AML_TS_CFG_REG6                     0x800 + (0x6  << 2 )
#define AML_TS_CFG_REG7                     0x800 + (0x7  << 2 )
#define AML_TS_CFG_REG8                     0x800 + (0x8  << 2 )
#define AML_TS_STAT0                        0x800 + (0x10 << 2 )
#define AML_TS_STAT1                        0x800 + (0x11 << 2 )
#define AML_TEMP_CAL                        1
#define AML_TS_TEMP_MASK                    0xfff


#define AML_TS_HITEMP_EN                    BIT(31)
#define AML_TS_REBOOT_ALL_EN                BIT(30)
#define AML_TS_REBOOT_TIME                  (0xff << 16)

#define AML_TS_IRQ_LOGIC_EN_SHIFT           15
#define AML_TS_RSET_VBG                     BIT(12)
#define AML_TS_RSET_ADC                     BIT(11)
#define AML_TS_VCM_EN                       BIT(10)
#define AML_TS_VBG_EN                       BIT(9)
#define AML_TS_OUT_CTL                      BIT(6)
#define AML_TS_FILTER_EN                    BIT(5)
#define AML_TS_IPTAT_EN                     BIT(4)  /* for debug, no need enable */
#define AML_TS_DEM_EN                       BIT(3)
#define AML_TS_CH_SEL                       0x3     /* set 3'b011 for work */

#define AML_HHI_TS_CLK_ENABLE               0x130U   /* u-boot */
