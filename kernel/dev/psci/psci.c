// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/psci.h>

#if WITH_DEV_PDEV
#include <pdev/driver.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#endif

// in psci.S
extern uint64_t psci_smc_call(ulong arg0, ulong arg1, ulong arg2, ulong arg3);
extern uint64_t psci_hvc_call(ulong arg0, ulong arg1, ulong arg2, ulong arg3);

#if PSCI_USE_HVC
psci_call_proc do_psci_call = psci_hvc_call;
#else
psci_call_proc do_psci_call = psci_smc_call;
#endif

#if WITH_DEV_PDEV
static void arm_psci_init(mdi_node_ref_t* node, uint level) {
    bool use_smc = false;
    bool use_hvc = false;

    mdi_node_ref_t child;
    mdi_each_child(node, &child) {
        switch (mdi_id(&child)) {
        case MDI_ARM_PSCI_USE_SMC:
            mdi_node_boolean(&child, &use_smc);
            break;
        case MDI_ARM_PSCI_USE_HVC:
            mdi_node_boolean(&child, &use_hvc);
            break;
        }
    }

    if (use_smc && use_hvc) {
        panic("both use-smc and use-hvc set in arm_psci_init\n");
    }
    if (!use_smc && !use_hvc) {
        panic("neither use-smc and use-hvc set in arm_psci_init\n");
    }
    do_psci_call = use_smc ? psci_smc_call : psci_hvc_call;
}

LK_PDEV_INIT(arm_psci_init, MDI_ARM_PSCI, arm_psci_init, LK_INIT_LEVEL_PLATFORM_EARLY);
#endif // WITH_DEV_PDEV
