// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <debug.h>
#include <inttypes.h>
#include <lib/arch/intrin.h>

#include <arch/arm64/smccc.h>
#include <arch/interrupt.h>
#include <dev/power.h>
#include <dev/power/motmot/init.h>
#include <dev/psci.h>
#include <pdev/power.h>

// A small driver that sends commands to EL3 in order to twiddle the registers
// needed to either power down, or reboot the target.  Note that while we can
// see these registers in EL1, writes to them are squashed.

namespace {

// The command ID we send via SMC in order to modify registers
constexpr uint32_t SMC_CMD_PRIV_REG = 0x82000504;

// Options for what to do with the register (read, write, RMW)
[[maybe_unused]] constexpr uint32_t PRIV_REG_OPTION_READ = 0;
[[maybe_unused]] constexpr uint32_t PRIV_REG_OPTION_WRITE = 1;
constexpr uint32_t PRIV_REG_OPTION_RMW = 2;

// The base physical address of the PMU
constexpr uintptr_t PMU_ALIVE_BASE = 0x17460000;

// PMU docs, section 1.6.176
constexpr uintptr_t SYSTEM_CONFIGURATION_REG = (PMU_ALIVE_BASE + 0x3a00);
constexpr uint32_t SWRESET_SYSTEM = (1 << 1);

// PMU docs, section 1.6.312
constexpr uintptr_t PAD_CTRL_PWR_HOLD_REG = (PMU_ALIVE_BASE + 0x3e9c);
constexpr uint32_t PS_HOLD_CTRL_DATA = (1 << 8);

inline uint64_t modify_register_via_smc(uintptr_t phys_addr, uint32_t mask, uint32_t val) {
  const arm_smccc_result_t res =
      arm_smccc_smc_internal(SMC_CMD_PRIV_REG, phys_addr, PRIV_REG_OPTION_RMW, mask, val, 0, 0, 0);
  return res.x0;
}

void motmot_reboot(enum reboot_flags flags) {
  uint64_t result;

  switch (flags) {
    case REBOOT_BOOTLOADER:
    case REBOOT_RECOVERY:
      dprintf(INFO, "Motmot does not support rebooting into recover or bootloader yet.\n");
      __FALLTHROUGH;

    case REBOOT_NORMAL:
      dprintf(INFO, "Sending reboot command via SMC\n");
      result = modify_register_via_smc(SYSTEM_CONFIGURATION_REG, SWRESET_SYSTEM, SWRESET_SYSTEM);
      modify_register_via_smc(SYSTEM_CONFIGURATION_REG, SWRESET_SYSTEM, SWRESET_SYSTEM);
      dprintf(INFO, "Reboot command failed, result was %" PRIx64 ".\n", result);
      break;

    default:
      dprintf(INFO, "Bad reboot flag 0x%08x\n", static_cast<uint32_t>(flags));
      break;
  }
}

void motmot_shutdown() {
  dprintf(INFO, "Sending shutdown command via SMC\n");
  const uint64_t result = modify_register_via_smc(PAD_CTRL_PWR_HOLD_REG, PS_HOLD_CTRL_DATA, 0);
  dprintf(INFO, "Shutdown command failed, result was %" PRIx64 ".\n", result);
}

uint32_t motmot_cpu_off() {
  // TODO(johngro):  Figure out how to properly power down our CPU on motmot.
  // It does not current respond to the PSCI command to turn off the current
  // CPU, and I have not found the proper bits in the HW to twiddle in order to
  // shut down the CPU.
  //
  // Since we only really call this function when we are shutting down or
  // rebooting, we simply shut off interrupts and spin on WFI for now.
  // Eventually, (when we start to turn CPUs on and off during normal operation)
  // we will need to come back here and figure out the proper thing to do.
  InterruptDisableGuard irqd;
  while (true) {
    __wfi();
  }
  return 0;
}

const struct pdev_power_ops motmot_power_ops = {
    .reboot = motmot_reboot,
    .shutdown = motmot_shutdown,
    .cpu_off = motmot_cpu_off,
    .cpu_on = psci_cpu_on,
};

}  // namespace

void motmot_power_init_early() { pdev_register_power(&motmot_power_ops); }
