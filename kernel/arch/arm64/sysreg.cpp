// Copyright 2017 The Fuchsia Authors
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64.h>
#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>

#if ARCH_ARM64
#if WITH_LIB_CONSOLE
#include <lib/console.h>

#define SYSREG_READ_COMMAND(sysreg_string)                                      \
    if (!strncasecmp(regname, #sysreg_string, sizeof(#sysreg_string))) {        \
        printf(#sysreg_string " = %016lx\n", ARM64_READ_SYSREG(sysreg_string)); \
        return 0;                                                               \
    } else

static uint64_t read_sysregs(const char* regname) {
    SYSREG_READ_COMMAND(ACTLR_EL1)
    SYSREG_READ_COMMAND(CCSIDR_EL1)
    SYSREG_READ_COMMAND(CLIDR_EL1)
    SYSREG_READ_COMMAND(CSSELR_EL1)
    SYSREG_READ_COMMAND(MIDR_EL1)
    SYSREG_READ_COMMAND(MPIDR_EL1)
    SYSREG_READ_COMMAND(SCTLR_EL1)
    SYSREG_READ_COMMAND(SPSR_EL1)
    SYSREG_READ_COMMAND(TCR_EL1)
    SYSREG_READ_COMMAND(TPIDRRO_EL0)
    SYSREG_READ_COMMAND(TPIDR_EL1)
    SYSREG_READ_COMMAND(TTBR0_EL1)
    SYSREG_READ_COMMAND(TTBR1_EL1)
    SYSREG_READ_COMMAND(VBAR_EL1)

    //Generic Timer regs
    SYSREG_READ_COMMAND(CNTFRQ_EL0)
    SYSREG_READ_COMMAND(CNTKCTL_EL1)
    SYSREG_READ_COMMAND(CNTPCT_EL0)
    SYSREG_READ_COMMAND(CNTPS_CTL_EL1)
    SYSREG_READ_COMMAND(CNTPS_CVAL_EL1)
    SYSREG_READ_COMMAND(CNTPS_TVAL_EL1)
    SYSREG_READ_COMMAND(CNTP_CTL_EL0)
    SYSREG_READ_COMMAND(CNTP_CVAL_EL0)
    SYSREG_READ_COMMAND(CNTP_TVAL_EL0)
    SYSREG_READ_COMMAND(CNTVCT_EL0)
    SYSREG_READ_COMMAND(CNTV_CTL_EL0)
    SYSREG_READ_COMMAND(CNTV_CVAL_EL0)
    SYSREG_READ_COMMAND(CNTV_TVAL_EL0) {
        printf("Could not find register %s in list (you may need to add it to kernel/kernel/sysreg.c)\n", regname);
    }
    return 0;
}

static int cmd_sysreg(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND("sysreg", "read armv8 system register", &cmd_sysreg)
STATIC_COMMAND_END(kernel);

static int cmd_sysreg(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
        printf("not enough arguments\n");
        return -1;
    }
    read_sysregs(argv[1].str);
    return 0;
}

#endif // WITH_LIB_CONSOLE
#endif // ARCH_ARM64
