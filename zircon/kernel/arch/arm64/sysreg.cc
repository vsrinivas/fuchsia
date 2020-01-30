// Copyright 2017 The Fuchsia Authors
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdio.h>
#include <string.h>

#include <arch/arm64.h>

#if ARCH_ARM64
#include <lib/console.h>

#define SYSREG_READ_COMMAND(sysreg_string)                           \
  if (!strncasecmp(regname, sysreg_string, sizeof(sysreg_string))) { \
    printf(sysreg_string " = %016lx\n", __arm_rsr64(sysreg_string)); \
    return 0;                                                        \
  } else

static uint64_t read_sysregs(const char* regname) {
  SYSREG_READ_COMMAND("actlr_el1")
  SYSREG_READ_COMMAND("ccsidr_el1")
  SYSREG_READ_COMMAND("clidr_el1")
  SYSREG_READ_COMMAND("csselr_el1")
  SYSREG_READ_COMMAND("midr_el1")
  SYSREG_READ_COMMAND("mpidr_el1")
  SYSREG_READ_COMMAND("sctlr_el1")
  SYSREG_READ_COMMAND("spsr_el1")
  SYSREG_READ_COMMAND("tcr_el1")
  SYSREG_READ_COMMAND("tpidrro_el0")
  SYSREG_READ_COMMAND("tpidr_el1")
  SYSREG_READ_COMMAND("ttbr0_el1")
  SYSREG_READ_COMMAND("ttbr1_el1")
  SYSREG_READ_COMMAND("vbar_el1")

  // Generic Timer regs
  SYSREG_READ_COMMAND("cntfrq_el0")
  SYSREG_READ_COMMAND("cntkctl_el1")
  SYSREG_READ_COMMAND("cntpct_el0")
  SYSREG_READ_COMMAND("cntps_ctl_el1")
  SYSREG_READ_COMMAND("cntps_cval_el1")
  SYSREG_READ_COMMAND("cntps_tval_el1")
  SYSREG_READ_COMMAND("cntp_ctl_el0")
  SYSREG_READ_COMMAND("cntp_cval_el0")
  SYSREG_READ_COMMAND("cntp_tval_el0")
  SYSREG_READ_COMMAND("cntvct_el0")
  SYSREG_READ_COMMAND("cntv_ctl_el0")
  SYSREG_READ_COMMAND("cntv_cval_el0")
  SYSREG_READ_COMMAND("cntv_tval_el0") {
    printf(
        "Could not find register %s in list (you may need to add it to kernel/kernel/sysreg.c)\n",
        regname);
  }
  return 0;
}

static const char * sysregs_list[] = {
  "actlr_el1", "ccsidr_el1", "clidr_el1", "csselr_el1", "midr_el1", "mpidr_el1",
  "sctlr_el1", "spsr_el1", "tcr_el1", "tpidrro_el0", "tpidr_el1", "ttbr0_el1",
  "ttbr1_el1", "vbar_el1", "cntfrq_el0", "cntkctl_el1", "cntpct_el0", "cntps_ctl_el1",
  "cntps_cval_el1", "cntps_tval_el1", "cntp_ctl_el0", "cntp_cval_el0", "cntp_tval_el0",
  "cntvct_el0", "cntv_ctl_el0", "cntv_cval_el0", "cntv_tval_el0" };

static void print_sysregs_list() {
  int size = countof(sysregs_list);
  printf(" system register name: \n");
  for (int i = 0; i < size; i++) {
    printf("      %s \n",sysregs_list[i]);
  }
}

static int cmd_sysreg(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND("sysreg", "read armv8 system register", &cmd_sysreg)
STATIC_COMMAND_END(kernel)

static int cmd_sysreg(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("not enough arguments\n");
    printf("usage:\n");
    printf("sysreg list \n");
    printf("sysreg <register_name> \n");
    return -1;
  }
  if (!strcmp(argv[1].str, "list")) {
    print_sysregs_list();
  } else {
    read_sysregs(argv[1].str);
  }

  return 0;
}

#endif  // ARCH_ARM64
