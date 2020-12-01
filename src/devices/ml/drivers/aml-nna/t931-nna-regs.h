// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_ML_DRIVERS_AML_NNA_T931_NNA_REGS_H_
#define SRC_DEVICES_ML_DRIVERS_AML_NNA_T931_NNA_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

#include "aml-nna.h"

static aml_nna::AmlNnaDevice::NnaBlock T931NnaBlock{
    // AO_RTI_GEN_PWR_SLEEP0
    .domain_power_sleep_offset = 0x3a << 2,
    // AO_RTI_GEN_PWR_ISO0
    .domain_power_iso_offset = 0x3b << 2,
    .domain_power_sleep_bits = (1 << 16) | (1 << 17),
    .domain_power_iso_bits = (1 << 16) | (1 << 17),

    // HHI_NANOQ_MEM_PD
    .hhi_mem_pd_reg0_offset = 0x43 << 2,
    .hhi_mem_pd_reg1_offset = 0x44 << 2,

    // RESET2_LEVEL
    .reset_level2_offset = aml_registers::RESET2_LEVEL_OFFSET,

    // HHI_VIPNANOQ_CLK_CNTL
    .clock_control_offset = 0x72 << 2,
};

#endif  // SRC_DEVICES_ML_DRIVERS_AML_NNA_T931_NNA_REGS_H_
