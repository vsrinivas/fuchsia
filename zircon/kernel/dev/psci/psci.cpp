// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/psci.h>

#include <arch/arm64/smccc.h>
#include <inttypes.h>
#include <pdev/driver.h>
#include <zircon/boot/driver-config.h>
#include <string.h>

static uint64_t shutdown_args[3] = { 0, 0, 0 };
static uint64_t reboot_args[3] = { 0, 0, 0 };
static uint64_t reboot_bootloader_args[3] = { 0, 0, 0 };
static uint64_t reboot_recovery_args[3] = { 0, 0, 0 };

static uint64_t psci_smc_call(uint32_t function, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    return arm_smccc_smc(function, arg0, arg1, arg2, 0, 0, 0, 0).x0;
}

static uint64_t psci_hvc_call(uint32_t function, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
    return arm_smccc_hvc(function, arg0, arg1, arg2, 0, 0, 0, 0).x0;
}

#if PSCI_USE_HVC
psci_call_proc do_psci_call = psci_hvc_call;
#else
psci_call_proc do_psci_call = psci_smc_call;
#endif

void psci_system_off() {
    do_psci_call(PSCI64_SYSTEM_OFF, shutdown_args[0], shutdown_args[1], shutdown_args[2]);
}

void psci_system_reset(enum reboot_flags flags) {
    uint64_t* args = reboot_args;

    if (flags == REBOOT_BOOTLOADER) {
        args = reboot_bootloader_args;
    } else if (flags == REBOOT_RECOVERY) {
        args = reboot_recovery_args;
    }

    do_psci_call(PSCI64_SYSTEM_RESET, args[0], args[1], args[2]);
}

static void arm_psci_init(const void* driver_data, uint32_t length) {
#if 0
    // TODO: restore this after everyone is updated to new bootloaders
    ASSERT(length >= sizeof(dcfg_arm_psci_driver_t));
#else
    ASSERT(length >= sizeof(dcfg_arm_psci_driver_t) - sizeof(reboot_recovery_args));
#endif

    auto driver = static_cast<const dcfg_arm_psci_driver_t*>(driver_data);

    do_psci_call = driver->use_hvc ? psci_hvc_call : psci_smc_call;
    memcpy(shutdown_args, driver->shutdown_args, sizeof(shutdown_args));
    memcpy(reboot_args, driver->reboot_args, sizeof(reboot_args));
    memcpy(reboot_bootloader_args, driver->reboot_bootloader_args, sizeof(reboot_bootloader_args));

    // TODO: remove this check after everyone is updated to new bootloaders
    if (length >= sizeof(dcfg_arm_psci_driver_t)) {
        memcpy(reboot_recovery_args, driver->reboot_recovery_args, sizeof(reboot_recovery_args));
    }
}

LK_PDEV_INIT(arm_psci_init, KDRV_ARM_PSCI, arm_psci_init, LK_INIT_LEVEL_PLATFORM_EARLY);

#include <lib/console.h>

static int cmd_psci(int argc, const cmd_args *argv, uint32_t flags) {
    if (argc < 2) {
        printf("not enough arguments\n");
        printf("%s function [arg0] [arg1] [arg2]\n", argv[0].str);
        return -1;
    }

    uint32_t function = static_cast<uint32_t>(argv[1].u);
    uint64_t arg0 = (argc >= 3) ? argv[2].u : 0;
    uint64_t arg1 = (argc >= 4) ? argv[3].u : 0;
    uint64_t arg2 = (argc >= 5) ? argv[4].u : 0;

    uint64_t ret = do_psci_call(function, arg0, arg1, arg2);
    printf("do_psci_call returned %" PRIu64 "\n", ret);
    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("psci", "execute PSCI command", &cmd_psci, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(psci);
