// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <string.h>
#include <trace.h>
#include <zircon/boot/driver-config.h>

#include <arch/arm64/smccc.h>
#include <dev/psci.h>

#define LOCAL_TRACE 0

// Defined in start.S.
extern paddr_t kernel_entry_paddr;

static uint64_t shutdown_args[3] = {0, 0, 0};
static uint64_t reboot_args[3] = {0, 0, 0};
static uint64_t reboot_bootloader_args[3] = {0, 0, 0};
static uint64_t reboot_recovery_args[3] = {0, 0, 0};
static uint32_t reset_command = PSCI64_SYSTEM_RESET;

static uint64_t psci_smc_call(uint32_t function, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
  return arm_smccc_smc(function, arg0, arg1, arg2, 0, 0, 0, 0).x0;
}

static uint64_t psci_hvc_call(uint32_t function, uint64_t arg0, uint64_t arg1, uint64_t arg2) {
  return arm_smccc_hvc(function, arg0, arg1, arg2, 0, 0, 0, 0).x0;
}

typedef uint64_t (*psci_call_proc)(uint32_t function, uint64_t arg0, uint64_t arg1, uint64_t arg2);

static psci_call_proc do_psci_call = psci_smc_call;

void psci_system_off() {
  do_psci_call(PSCI64_SYSTEM_OFF, shutdown_args[0], shutdown_args[1], shutdown_args[2]);
}

uint32_t psci_get_version() { return (uint32_t)do_psci_call(PSCI64_PSCI_VERSION, 0, 0, 0); }

/* powers down the calling cpu - only returns if call fails */
uint32_t psci_cpu_off() { return (uint32_t)do_psci_call(PSCI64_CPU_OFF, 0, 0, 0); }

uint32_t psci_cpu_on(uint64_t mpid, paddr_t entry) {
  LTRACEF("CPU_ON mpid %#" PRIx64 ", entry %#" PRIx64 "\n", mpid, entry);
  return (uint32_t)do_psci_call(PSCI64_CPU_ON, mpid, entry, 0);
}

uint32_t psci_get_affinity_info(uint64_t cluster, uint64_t cpuid) {
  return (uint32_t)do_psci_call(PSCI64_AFFINITY_INFO, ARM64_MPID(cluster, cpuid), 0, 0);
}

uint32_t psci_get_feature(uint32_t psci_call) {
  return (uint32_t)do_psci_call(PSCI64_PSCI_FEATURES, psci_call, 0, 0);
}

void psci_system_reset(enum reboot_flags flags) {
  uint64_t* args = reboot_args;

  if (flags == REBOOT_BOOTLOADER) {
    args = reboot_bootloader_args;
  } else if (flags == REBOOT_RECOVERY) {
    args = reboot_recovery_args;
  }

  dprintf(INFO, "PSCI reboot: %#" PRIx32 " %#" PRIx64 " %#" PRIx64 " %#" PRIx64 "\n", reset_command,
          args[0], args[1], args[2]);
  do_psci_call(reset_command, args[0], args[1], args[2]);
}

void PsciInit(const dcfg_arm_psci_driver_t& config) {
  do_psci_call = config.use_hvc ? psci_hvc_call : psci_smc_call;
  memcpy(shutdown_args, config.shutdown_args, sizeof(shutdown_args));
  memcpy(reboot_args, config.reboot_args, sizeof(reboot_args));
  memcpy(reboot_bootloader_args, config.reboot_bootloader_args, sizeof(reboot_bootloader_args));
  memcpy(reboot_recovery_args, config.reboot_recovery_args, sizeof(reboot_recovery_args));

  // read information about the psci implementation
  uint32_t result = psci_get_version();
  uint32_t major = (result >> 16) & 0xffff;
  uint32_t minor = result & 0xffff;
  dprintf(INFO, "PSCI version %u.%u\n", major, minor);

  if (major >= 1 && major != 0xffff) {
    // query features
    dprintf(INFO, "PSCI supported features:\n");
    result = psci_get_feature(PSCI64_SYSTEM_OFF);
    dprintf(INFO, "\tPSCI64_SYSTEM_OFF %#x\n", result);
    result = psci_get_feature(PSCI64_SYSTEM_RESET);
    dprintf(INFO, "\tPSCI64_SYSTEM_RESET %#x\n", result);
    result = psci_get_feature(PSCI64_SYSTEM_RESET2);
    dprintf(INFO, "\tPSCI64_SYSTEM_RESET2 %#x\n", result);
    if (result == 0) {
      // Prefer RESET2 if present. It explicitly supports arguments, but some vendors have
      // extended RESET to behave the same way.
      reset_command = PSCI64_SYSTEM_RESET2;
    }
    result = psci_get_feature(PSCI64_CPU_ON);
    dprintf(INFO, "\tPSCI64_CPU_ON %#x\n", result);
    result = psci_get_feature(PSCI64_CPU_OFF);
    dprintf(INFO, "\tPSCI64_CPU_OFF %#x\n", result);
  }
}

#include <lib/console.h>

static int cmd_psci(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
  notenoughargs:
    printf("not enough arguments\n");
    printf("%s system_reset\n", argv[0].str);
    printf("%s system_off\n", argv[0].str);
    printf("%s cpu_on <mpidr>\n", argv[0].str);
    printf("%s affinity_info <cluster> <cpu>\n", argv[0].str);
    printf("%s <function_id> [arg0] [arg1] [arg2]\n", argv[0].str);
    return -1;
  }

  if (!strcmp(argv[1].str, "system_reset")) {
    psci_system_reset(REBOOT_NORMAL);
  } else if (!strcmp(argv[1].str, "system_off")) {
    psci_system_off();
  } else if (!strcmp(argv[1].str, "cpu_on")) {
    if (argc < 3) {
      goto notenoughargs;
    }
    uint32_t ret = psci_cpu_on(argv[2].u, kernel_entry_paddr);
    printf("psci_cpu_on returns %u\n", ret);
  } else if (!strcmp(argv[1].str, "affinity_info")) {
    if (argc < 4) {
      goto notenoughargs;
    }
    uint32_t ret = psci_get_affinity_info(argv[2].u, argv[3].u);
    printf("affinity info returns %u\n", ret);
  } else {
    uint32_t function = static_cast<uint32_t>(argv[1].u);
    uint64_t arg0 = (argc >= 3) ? argv[2].u : 0;
    uint64_t arg1 = (argc >= 4) ? argv[3].u : 0;
    uint64_t arg2 = (argc >= 5) ? argv[4].u : 0;

    uint64_t ret = do_psci_call(function, arg0, arg1, arg2);
    printf("do_psci_call returned %" PRIu64 "\n", ret);
  }
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND_MASKED("psci", "execute PSCI command", &cmd_psci, CMD_AVAIL_ALWAYS)
STATIC_COMMAND_END(psci)
