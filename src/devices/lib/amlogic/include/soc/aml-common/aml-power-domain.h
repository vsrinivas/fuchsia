// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_POWER_DOMAIN_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_POWER_DOMAIN_H_

#include <zircon/syscalls/smc.h>

#define A5_PDID_NNA 0
#define A5_PDID_AUDIO 1
#define A5_PDID_SDIOA 2
#define A5_PDID_EMMC 3
#define A5_PDID_USB_COMB 4
#define A5_PDID_ETH 5
#define A5_PDID_RSA 6
#define A5_PDID_AUDIO_PDM 7
#define A5_PDID_DMC 8
#define A5_PDID_SYS_WRAP 9
#define A5_PDID_DSPA 10

namespace aml_pd_smc {

// Parameters:
// arg1 : power domain id
// arg2 : 1 - turn on power
//        0 - turn off power
//
static constexpr zx_smc_parameters_t CreatePdSmcCall(uint64_t arg1, uint64_t arg2,
                                                     uint64_t arg3 = 0, uint64_t arg4 = 0,
                                                     uint64_t arg5 = 0, uint64_t arg6 = 0,
                                                     uint16_t client_id = 0,
                                                     uint16_t secure_os_id = 0) {
  constexpr uint32_t kPowerDomainCtrlFuncId = 0x82000093;
  return {kPowerDomainCtrlFuncId, {}, arg1, arg2, arg3, arg4, arg5, arg6, client_id,
          secure_os_id,           {}};
}

}  // namespace aml_pd_smc

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_POWER_DOMAIN_H_
