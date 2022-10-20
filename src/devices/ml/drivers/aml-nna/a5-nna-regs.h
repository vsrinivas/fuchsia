// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ML_DRIVERS_AML_NNA_A5_NNA_REGS_H_
#define SRC_DEVICES_ML_DRIVERS_AML_NNA_A5_NNA_REGS_H_

#include "aml-nna.h"

static aml_nna::AmlNnaDevice::NnaBlock A5NnaBlock{
    .nna_power_version = kNnaPowerDomain,
    // RESET2_LEVEL
    .nna_domain_id = A5_PDID_NNA,

    // HHI_NN_CLK_CNTL
    .clock_control_offset = 0x88 << 2,
    // Bit[8]     - Core Clock Gate
    // Bit[11:9]  - Core Clock Source (1 - fclk_div2p5 800M)
    .clock_core_control_bits = (1 << 8) | (1 << 9),
    // Bit[24]     - Axi Clock Gate
    // Bit[27:25]  - Axi Clock Source (1 - fclk_div2p5 800M)
    .clock_axi_control_bits = (1 << 24) | (1 << 25),
};

#endif  // SRC_DEVICES_ML_DRIVERS_AML_NNA_A5_NNA_REGS_H_
