// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

.section .text

/* used to call into PSCI firmware (Power State Coordination Firmware) */
FUNCTION(psci_smc_call)
#if WITH_HYPERVISOR
    hvc     #0
#else
    smc     #0
#endif
    ret

.ltorg

