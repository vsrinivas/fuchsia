// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <debug.h>
#include <lib/arch/intrin.h>
#include <lib/cmdline.h>
#include <lib/console.h>
#include <lib/debuglog.h>
#include <lib/memory_limit.h>
#include <lib/system-topology.h>
#include <mexec.h>
#include <platform.h>
#include <reg.h>
#include <trace.h>

#include <arch/arch_ops.h>
#include <arch/riscv64.h>
#include <arch/mp.h>
#include <dev/display.h>
#include <dev/hw_rng.h>
#include <dev/interrupt.h>
#include <dev/power.h>
#include <dev/uart.h>
#include <explicit-memory/bytes.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <kernel/dpc.h>
#include <kernel/spinlock.h>
#include <ktl/atomic.h>
#include <lk/init.h>
#include <object/resource_dispatcher.h>
#include <platform/crashlog.h>
#include <vm/bootreserve.h>
#include <vm/kstack.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#if WITH_PANIC_BACKTRACE
#include <kernel/thread.h>
#endif

#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/syscalls/smc.h>
#include <zircon/types.h>

#include <pdev/pdev.h>

// Defined in start.S.
extern paddr_t zbi_paddr;

static zbi_header_t* zbi_root = nullptr;

const zbi_header_t* platform_get_zbi(void) { return zbi_root; }

void platform_panic_start(void) {
}

void* platform_get_ramdisk(size_t* size) {
    return NULL;
}

void platform_halt_cpu(void) {
}

void platform_early_init(void) {
}

void platform_prevm_init() {}

// Called after the heap is up but before the system is multithreaded.
void platform_init_pre_thread(uint) { }

LK_INIT_HOOK(platform_init_pre_thread, platform_init_pre_thread, LK_INIT_LEVEL_VM)

void platform_init(void) { }

// after the fact create a region to reserve the peripheral map(s)
static void platform_init_postvm(uint level) { }

LK_INIT_HOOK(platform_postvm, platform_init_postvm, LK_INIT_LEVEL_VM)

void platform_dputs_thread(const char* str, size_t len) {
}

void platform_dputs_irq(const char* str, size_t len) {
}

int platform_dgetc(char* c, bool wait) {
  return 0;
}

void platform_pputc(char c) {
}

int platform_pgetc(char* c, bool wait) {
  return 0;
}

/* no built in framebuffer */
zx_status_t display_get_info(struct display_info* info) { return ZX_ERR_NOT_FOUND; }

void platform_specific_halt(platform_halt_action suggested_action, zircon_crash_reason_t reason,
                            bool halt_on_panic) {
  for (;;)
    ;
}

zx_status_t platform_mexec_patch_zbi(uint8_t* zbi, const size_t len) {
  return ZX_OK;
}

void platform_mexec_prep(uintptr_t new_bootimage_addr, size_t new_bootimage_len) {
}

void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops, uintptr_t new_bootimage_addr,
                    size_t new_bootimage_len, uintptr_t entry64_addr) {
}

bool platform_serial_enabled(void) { return true; }

bool platform_early_console_enabled() { return true; }

// Initialize Resource system after the heap is initialized.
static void riscv64_resource_dispatcher_init_hook(unsigned int rl) {
}

LK_INIT_HOOK(riscv64_resource_init, riscv64_resource_dispatcher_init_hook, LK_INIT_LEVEL_HEAP)

void topology_init() {
}

void platform_stop_timer(void) {
}

zx_ticks_t platform_current_ticks() {
  return 0;
}

zx_status_t platform_set_oneshot_timer(zx_time_t deadline) {
  return ZX_OK;
}

zx_status_t register_int_handler(unsigned int vector, int_handler handler, void* arg) {
  return ZX_OK;
}

bool msi_supports_masking() { return false; }

void msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask) { PANIC_UNIMPLEMENTED; }

zx_status_t mask_interrupt(unsigned int vector) { return ZX_OK; }

bool platform_usermode_can_access_tick_registers(void) { return false; }

void shutdown_interrupts(void) { }

void shutdown_interrupts_curr_cpu(void) {
}

zx_status_t unmask_interrupt(unsigned int vector) {
  PANIC_UNIMPLEMENTED;
}

zx_status_t configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                                enum interrupt_polarity pol) {
  PANIC_UNIMPLEMENTED;
}

bool is_valid_interrupt(unsigned int vector, uint32_t flags) {
  return false;
}

unsigned int remap_interrupt(unsigned int vector) {
  return false;
}

zx_status_t msi_alloc_block(uint requested_irqs, bool can_target_64bit, bool is_msix,
                            msi_block_t* out_block) {
  PANIC_UNIMPLEMENTED;
}

void msi_free_block(msi_block_t* block) {
  PANIC_UNIMPLEMENTED;
}

bool msi_is_supported(void) {
  return false;
}

void msi_register_handler(const msi_block_t* block, uint msi_id, int_handler handler, void* ctx) {
  PANIC_UNIMPLEMENTED;
}

zx_status_t platform_mp_prep_cpu_unplug(cpu_num_t cpu_id) {
  return arch_mp_prep_cpu_unplug(cpu_id);
}

zx_status_t platform_mp_cpu_unplug(cpu_num_t cpu_id) { return arch_mp_cpu_unplug(cpu_id); }

zx_status_t platform_append_mexec_data(fbl::Span<std::byte> data_zbi)  {
  return ZX_OK;
}
