// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "platform/pc/acpi.h"

#include <align.h>
#include <lib/acpi_lite.h>
#include <lib/acpi_lite/structures.h>
#include <lib/acpi_lite/zircon.h>
#include <lib/console.h>
#include <lib/fit/defer.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/zx/status.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/x86/acpi.h>
#include <arch/x86/bootstrap16.h>
#include <kernel/percpu.h>
#include <ktl/optional.h>

#include <ktl/enforce.h>

namespace {

// The FACS signature is 4 a byte ascii string "FACS" represented as an integer stored in
// little-endian format.
constexpr uint32_t kFacsSig = 0x53'43'41'46;

// System-wide ACPI parser.
acpi_lite::AcpiParser* global_acpi_parser;

lazy_init::LazyInit<acpi_lite::AcpiParser, lazy_init::CheckType::None,
                    lazy_init::Destructor::Disabled>
    g_parser;

int ConsoleAcpiDump(int argc, const cmd_args* argv, uint32_t flags) {
  if (global_acpi_parser == nullptr) {
    printf("ACPI not initialized.\n");
    return 1;
  }

  global_acpi_parser->DumpTables();
  return 0;
}

}  // namespace

acpi_lite::AcpiParser& GlobalAcpiLiteParser() {
  ASSERT_MSG(global_acpi_parser != nullptr, "PlatformInitAcpi() not called.");
  return *global_acpi_parser;
}

void PlatformInitAcpi(zx_paddr_t acpi_rsdp) {
  ASSERT(global_acpi_parser == nullptr);

  // Create AcpiParser.
  zx::result<acpi_lite::AcpiParser> result = acpi_lite::AcpiParserInit(acpi_rsdp);
  if (result.is_error()) {
    panic("Could not initialize ACPI. Error code: %d.", result.error_value());
  }

  g_parser.Initialize(result.value());
  global_acpi_parser = &g_parser.Get();
}

zx_status_t PlatformSuspend(uint8_t target_s_state, uint8_t sleep_type_a, uint8_t sleep_type_b) {
  // Acquire resources for suspend and resume.
  x86_realmode_entry_data* bootstrap_data;
  struct x86_realmode_entry_data_registers regs;
  paddr_t bootstrap_ip;
  zx_status_t status;

  // Get the waking vector
  status = x86_bootstrap16_acquire(reinterpret_cast<uintptr_t>(_x86_suspend_wakeup),
                                   reinterpret_cast<void**>(&bootstrap_data), &bootstrap_ip);
  if (status != ZX_OK) {
    TRACEF("Suspend failed: could not get bootstrap data. Error code: %d\n", status);
    return ZX_ERR_INTERNAL;
  }

  auto bootstrap_cleanup =
      fit::defer([&bootstrap_data]() { x86_bootstrap16_release(bootstrap_data); });

  const acpi_lite::AcpiFadt* acpi_fadt =
      acpi_lite::GetTableByType<acpi_lite::AcpiFadt>(GlobalAcpiLiteParser());
  if (acpi_fadt == nullptr) {
    TRACEF("Suspend failed: Could not get FADT\n");
    return ZX_ERR_INTERNAL;
  }

  // Setup our resume path
  // As Fuchsia only supports 64-bit architectures we expect to be able to use the 64-bit physical
  // address of the FACS
  if (acpi_fadt->x_firmware_ctrl == 0) {
    return ZX_ERR_INTERNAL;
  }

  // Get the address of the page that the FACS table is on.
  const uint64_t page_address = ROUNDDOWN(acpi_fadt->x_firmware_ctrl, PAGE_SIZE);
  // Round up the page size in case the FACS table is offset across pages.
  const uint64_t facs_page_size = ROUNDUP(PAGE_SIZE + sizeof(acpi_lite::AcpiFacs), PAGE_SIZE);

  // Map page where FACS is stored.
  uint8_t* facs_page_addr;
  status = VmAspace::kernel_aspace()->AllocPhysical(
      "facs", facs_page_size,  /* size */
      (void**)&facs_page_addr, /* returned virtual address */
      PAGE_SIZE_SHIFT,         /* alignment log2 */
      page_address,            /* physical address */
      0,                       /* vmm flags */
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
  if (status != ZX_OK) {
    TRACEF("Suspend failed: Could not map FACS memory. Error code: %d\n", status);
    return ZX_ERR_INTERNAL;
  }

  auto facs_cleanup = fit::defer([facs_page_addr]() {
    VmAspace::kernel_aspace()->FreeRegion(reinterpret_cast<vaddr_t>(facs_page_addr));
  });

  // Add the offset of the table address to the pointer and cast to the table type.
  uint8_t* facs_addr = facs_page_addr + acpi_fadt->x_firmware_ctrl - page_address;
  struct acpi_lite::AcpiFacs* acpi_facs = reinterpret_cast<struct acpi_lite::AcpiFacs*>(facs_addr);

  if (acpi_facs->sig.value != kFacsSig || acpi_facs->length != sizeof(*acpi_facs)) {
    return ZX_ERR_INTERNAL;
  }

  // The 64-bit X Firmware Waking Vector allows the wake-up code to be called in Protected Mode.
  // However we use the 32-bit waking vector as our wake-up vector is in memory below 1MB so doesn't
  // need Protected Mode. Additionally, on resume we need to bring up our secondary cores which
  // start in 16-bit mode anyway.
  acpi_facs->firmware_waking_vector = static_cast<uint32_t>(bootstrap_ip);
  acpi_facs->x_firmware_waking_vector = 0;
  auto wake_vector_cleanup = fit::defer([acpi_facs]() { acpi_facs->firmware_waking_vector = 0; });

  bootstrap_data->registers_ptr = reinterpret_cast<uintptr_t>(&regs);

  // Disable interrupts before we save interrupt state
  InterruptDisableGuard interrupt_disable;

  // Save system state.
  platform_prep_suspend();
  arch_prep_suspend();

  status = x86_acpi_transition_s_state(&regs, target_s_state, sleep_type_a, sleep_type_b);

  if (status != ZX_OK) {
    TRACEF("Suspend failed: %d", status);
    arch_resume();
    platform_resume();
    return ZX_ERR_INTERNAL;
  }

  // We have resumed and need to restore our CPU context
  DEBUG_ASSERT(arch_ints_disabled());

  arch_resume();
  platform_resume();
  percpu::Get(arch_curr_cpu_num()).timer_queue.ThawPercpu();

  DEBUG_ASSERT(arch_ints_disabled());

  return ZX_OK;
}

STATIC_COMMAND_START
STATIC_COMMAND("acpidump", "dump ACPI tables to console", &ConsoleAcpiDump)
STATIC_COMMAND_END(vm)
