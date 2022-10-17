// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <debug.h>
#include <lib/arch/intrin.h>
#include <lib/boot-options/boot-options.h>
#include <lib/console.h>
#include <lib/crashlog.h>
#include <lib/debuglog.h>
#include <lib/instrumentation/asan.h>
#include <lib/jtrace/jtrace.h>
#include <lib/memory_limit.h>
#include <lib/persistent-debuglog.h>
#include <lib/system-topology.h>
#include <mexec.h>
#include <platform.h>
#include <reg.h>
#include <string-file.h>
#include <trace.h>

#include <arch/arch_ops.h>
#include <arch/arm64.h>
#include <arch/arm64/mmu.h>
#include <arch/arm64/mp.h>
#include <arch/arm64/periphmap.h>
#include <arch/mp.h>
#include <dev/display.h>
#include <dev/hw_rng.h>
#include <dev/interrupt.h>
#include <dev/power.h>
#include <dev/psci.h>
#include <dev/uart.h>
#include <explicit-memory/bytes.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <kernel/cpu.h>
#include <kernel/cpu_distance_map.h>
#include <kernel/dpc.h>
#include <kernel/persistent_ram.h>
#include <kernel/spinlock.h>
#include <kernel/topology.h>
#include <ktl/algorithm.h>
#include <ktl/atomic.h>
#include <ktl/byte.h>
#include <ktl/span.h>
#include <ktl/variant.h>
#include <lk/init.h>
#include <object/resource_dispatcher.h>
#include <phys/handoff.h>
#include <platform/crashlog.h>
#include <vm/bootreserve.h>
#include <vm/kstack.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

#include <ktl/enforce.h>

#if WITH_PANIC_BACKTRACE
#include <kernel/thread.h>
#endif

#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/syscalls/smc.h>
#include <zircon/types.h>

#include <platform/ram_mappable_crashlog.h>

// Defined in start.S.
extern paddr_t kernel_entry_paddr;

static bool uart_disabled = false;

// all of the configured memory arenas from the zbi
static constexpr size_t kNumArenas = 16;
static pmm_arena_info_t mem_arena[kNumArenas];
static size_t arena_count = 0;

static ktl::atomic<int> panic_started;
static ktl::atomic<int> halted;

namespace {

lazy_init::LazyInit<RamMappableCrashlog, lazy_init::CheckType::None,
                    lazy_init::Destructor::Disabled>
    ram_mappable_crashlog;

}  // namespace

static void halt_other_cpus(void) {
  if (halted.exchange(1) == 0) {
    // stop the other cpus
    printf("stopping other cpus\n");
    arch_mp_send_ipi(MP_IPI_TARGET_ALL_BUT_LOCAL, 0, MP_IPI_HALT);

    // spin for a while
    // TODO: find a better way to spin at this low level
    for (volatile int i = 0; i < 100000000; i = i + 1) {
      __asm volatile("nop");
    }
  }
}

// Difference on SMT systems is that the AFF0 (cpu_id) level is implicit and not stored in the info.
static uint64_t ToSmtMpid(const zbi_topology_processor_t& processor, uint8_t cpu_id) {
  DEBUG_ASSERT(processor.architecture == ZBI_TOPOLOGY_ARCH_ARM);
  const auto& info = processor.architecture_info.arm;
  return (uint64_t)info.cluster_3_id << 32 | info.cluster_2_id << 16 | info.cluster_1_id << 8 |
         cpu_id;
}

static uint64_t ToMpid(const zbi_topology_processor_t& processor) {
  DEBUG_ASSERT(processor.architecture == ZBI_TOPOLOGY_ARCH_ARM);
  const auto& info = processor.architecture_info.arm;
  return (uint64_t)info.cluster_3_id << 32 | info.cluster_2_id << 16 | info.cluster_1_id << 8 |
         info.cpu_id;
}

// TODO(fxbug.dev/98351): Refactor platform_panic_start.
void platform_panic_start(PanicStartHaltOtherCpus option) {
  arch_disable_ints();
  dlog_panic_start();

  if (option == PanicStartHaltOtherCpus::Yes) {
    halt_other_cpus();
  }

  if (panic_started.exchange(1) == 0) {
    dlog_bluescreen_init();
    // Attempt to dump the current debug trace buffer, if we have one.
    jtrace_dump(jtrace::TraceBufferType::Current);
  }
}

void platform_halt_cpu(void) {
  uint32_t result = power_cpu_off();
  // should have never returned
  panic("power_cpu_off returned %u\n", result);
}

static zx_status_t platform_start_cpu(cpu_num_t cpu_id, uint64_t mpid) {
  // Issue memory barrier before starting to ensure previous stores will be visible to new CPU.
  arch::ThreadMemoryBarrier();

  uint32_t ret = power_cpu_on(mpid, kernel_entry_paddr);
  dprintf(INFO, "Trying to start cpu %u, mpid %#" PRIx64 " returned: %d\n", cpu_id, mpid, (int)ret);
  if (ret != 0) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

static void topology_cpu_init(void) {
  for (auto* node : system_topology::GetSystemTopology().processors()) {
    if (node->entity_type != ZBI_TOPOLOGY_ENTITY_PROCESSOR ||
        node->entity.processor.architecture != ZBI_TOPOLOGY_ARCH_ARM) {
      panic("Invalid processor node.");
    }

    zx_status_t status;
    const auto& processor = node->entity.processor;
    for (uint8_t i = 0; i < processor.logical_id_count; i++) {
      const uint64_t mpid =
          (processor.logical_id_count > 1) ? ToSmtMpid(processor, i) : ToMpid(processor);
      arch_register_mpid(processor.logical_ids[i], mpid);

      // Skip processor 0, we are only starting secondary processors.
      if (processor.logical_ids[i] == 0) {
        continue;
      }

      status = arm64_create_secondary_stack(processor.logical_ids[i], mpid);
      DEBUG_ASSERT(status == ZX_OK);

      // start the cpu
      status = platform_start_cpu(processor.logical_ids[i], mpid);

      if (status != ZX_OK) {
        // TODO(maniscalco): Is continuing really the right thing to do here?

        // start failed, free the stack
        status = arm64_free_secondary_stack(processor.logical_ids[i]);
        DEBUG_ASSERT(status == ZX_OK);
        continue;
      }
    }
  }

  // Create a thread that checks that the secondary processors actually
  // started. Since the secondary cpus are defined in the bootloader by humans
  // it is possible they don't match the hardware.
  constexpr auto check_cpus_booted = [](void*) -> int {
    // We wait for secondary cpus to start up.
    Thread::Current::SleepRelative(ZX_SEC(5));

    // Check that all cpus in the topology are now online.
    const auto online_mask = mp_get_online_mask();
    for (auto* node : system_topology::GetSystemTopology().processors()) {
      const auto& processor = node->entity.processor;
      for (int i = 0; i < processor.logical_id_count; i++) {
        const auto logical_id = node->entity.processor.logical_ids[i];
        if ((cpu_num_to_mask(logical_id) & online_mask) == 0) {
          printf("ERROR: CPU %d did not start!\n", logical_id);
        }
      }
    }
    return 0;
  };

  auto* warning_thread = Thread::Create("platform-cpu-boot-check-thread", check_cpus_booted,
                                        nullptr, DEFAULT_PRIORITY);
  warning_thread->DetachAndResume();
}

static void process_mem_ranges(ktl::span<const zbi_mem_range_t> ranges) {
  // First process all the reserved ranges. We do this in case there are reserved regions that
  // overlap with the RAM regions that occur later in the list. If we didn't process the reserved
  // regions first, then we might add a pmm arena and have it carve out its vm_page_t array from
  // what we will later learn is reserved memory.
  for (const zbi_mem_range_t& mem_range : ranges) {
    if (mem_range.type == ZBI_MEM_RANGE_RESERVED) {
      dprintf(INFO, "ZBI: reserve mem range base %#" PRIx64 " size %#" PRIx64 "\n", mem_range.paddr,
              mem_range.length);
      boot_reserve_add_range(mem_range.paddr, mem_range.length);
    }
  }
  for (const zbi_mem_range_t& mem_range : ranges) {
    switch (mem_range.type) {
      case ZBI_MEM_RANGE_RAM:
        dprintf(INFO, "ZBI: mem arena base %#" PRIx64 " size %#" PRIx64 "\n", mem_range.paddr,
                mem_range.length);
        if (arena_count >= kNumArenas) {
          printf("ZBI: Warning, too many memory arenas, dropping additional\n");
          break;
        }
        mem_arena[arena_count] = pmm_arena_info_t{"ram", 0, mem_range.paddr, mem_range.length};
        arena_count++;
        break;
      case ZBI_MEM_RANGE_PERIPHERAL: {
        dprintf(INFO, "ZBI: peripheral range base %#" PRIx64 " size %#" PRIx64 "\n",
                mem_range.paddr, mem_range.length);
        auto status = add_periph_range(mem_range.paddr, mem_range.length);
        ASSERT(status == ZX_OK);
        break;
      }
      case ZBI_MEM_RANGE_RESERVED:
        // Already handled the reserved ranges.
        break;
      default:
        // Treat unknown memory range types as reserved.
        dprintf(INFO,
                "ZBI: unknown mem range base %#" PRIx64 " size %#" PRIx64 " (type %" PRIu32 ")\n",
                mem_range.paddr, mem_range.length, mem_range.type);
        boot_reserve_add_range(mem_range.paddr, mem_range.length);
        break;
    }
  }
}

static constexpr zbi_topology_node_t fallback_topology = {
    .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
    .parent_index = ZBI_TOPOLOGY_NO_PARENT,
    .entity = {.processor = {.logical_ids = {0},
                             .logical_id_count = 1,
                             .flags = 0,
                             .architecture = ZBI_TOPOLOGY_ARCH_ARM,
                             .architecture_info = {.arm = {
                                                       .cluster_1_id = 0,
                                                       .cluster_2_id = 0,
                                                       .cluster_3_id = 0,
                                                       .cpu_id = 0,
                                                       .gic_id = 0,
                                                   }}}}};

static void init_topology(uint level) {
  ktl::span handoff = gPhysHandoff->cpu_topology.get();

  auto result = system_topology::Graph::InitializeSystemTopology(handoff.data(), handoff.size());
  if (result != ZX_OK) {
    printf("Failed to initialize system topology! error: %d\n", result);

    // Try to fallback to a topology of just this processor.
    result = system_topology::Graph::InitializeSystemTopology(&fallback_topology, 1);
    ASSERT(result == ZX_OK);
  }

  arch_set_num_cpus(static_cast<uint>(system_topology::GetSystemTopology().processor_count()));

  // TODO(fxbug.dev/32903) Print the whole topology of the system.
  if (DPRINTF_ENABLED_FOR_LEVEL(INFO)) {
    for (auto* proc : system_topology::GetSystemTopology().processors()) {
      auto& info = proc->entity.processor.architecture_info.arm;
      dprintf(INFO, "System topology: CPU %u:%u:%u:%u\n", info.cluster_3_id, info.cluster_2_id,
              info.cluster_1_id, info.cpu_id);
    }
  }
}

LK_INIT_HOOK(init_topology, init_topology, LK_INIT_LEVEL_VM)

static void allocate_persistent_ram(paddr_t pa, size_t length) {
  // Figure out how to divide up our persistent RAM.  Right now there are
  // three potential users:
  //
  // 1) The crashlog.
  // 2) Persistent debug logging.
  // 3) Persistent debug tracing.
  //
  // Persistent debug logging and tracing have target amounts of RAM they would
  // _like_ to have, and crash-logging has a minimum amount it is guaranteed to
  // get.  Additionally, all allocated are made in a chunks of the minimum
  // persistent RAM allocation granularity.
  //
  // Make sure that the crashlog gets as much of its minimum allocation as is
  // possible.  Then attempt to satisfy the target for persistent debug logging,
  // followed by persistent debug tracing.  Finally, give anything leftovers to
  // the crashlog.
  size_t crashlog_size = 0;
  size_t pdlog_size = 0;
  size_t jtrace_size = 0;
  {
    // start by figuring out how many chunks of RAM we have available to
    // us total.
    size_t persistent_chunks_available = length / kPersistentRamAllocationGranularity;

    // If we have not already configured a non-trivial crashlog implementation
    // for the platform, make sure that crashlog gets its minimum allocation, or
    // all of the RAM if it cannot meet even its minimum allocation.
    size_t crashlog_chunks = !PlatformCrashlog::HasNonTrivialImpl()
                                 ? ktl::min(persistent_chunks_available,
                                            kMinCrashlogSize / kPersistentRamAllocationGranularity)
                                 : 0;
    persistent_chunks_available -= crashlog_chunks;

    // Next in line is persistent debug logging.
    size_t pdlog_chunks =
        ktl::min(persistent_chunks_available,
                 kTargetPersistentDebugLogSize / kPersistentRamAllocationGranularity);
    persistent_chunks_available -= pdlog_chunks;

    // Next up is persistent debug tracing.
    size_t jtrace_chunks =
        ktl::min(persistent_chunks_available,
                 kJTraceTargetPersistentBufferSize / kPersistentRamAllocationGranularity);
    persistent_chunks_available -= jtrace_chunks;

    // Finally, anything left over can go to the crashlog.
    crashlog_chunks += persistent_chunks_available;

    crashlog_size = crashlog_chunks * kPersistentRamAllocationGranularity;
    pdlog_size = pdlog_chunks * kPersistentRamAllocationGranularity;
    jtrace_size = jtrace_chunks * kPersistentRamAllocationGranularity;
  }

  // Configure up the crashlog RAM
  if (crashlog_size > 0) {
    dprintf(INFO, "Crashlog configured with %" PRIu64 " bytes\n", crashlog_size);
    ram_mappable_crashlog.Initialize(pa, crashlog_size);
    PlatformCrashlog::Bind(ram_mappable_crashlog.Get());
  }
  size_t offset = crashlog_size;

  // Configure the persistent debuglog RAM (if we have any)
  if (pdlog_size > 0) {
    dprintf(INFO, "Persistent debug logging enabled and configured with %" PRIu64 " bytes\n",
            pdlog_size);
    persistent_dlog_set_location(paddr_to_physmap(pa + offset), pdlog_size);
    offset += pdlog_size;
  }

  // Do _not_ attempt to set the location of the debug trace buffer if this is
  // not a persistent debug trace buffer.  The location of a non-persistent
  // trace buffer would have been already set during (very) early init.
  if constexpr (kJTraceIsPersistent == jtrace::IsPersistent::Yes) {
    jtrace_set_location(paddr_to_physmap(pa + offset), jtrace_size);
    offset += jtrace_size;
  }
}

// Called during platform_init_early.
static void ProcessPhysHandoff() {
  if (gPhysHandoff->nvram) {
    const zbi_nvram_t& nvram = gPhysHandoff->nvram.value();
    dprintf(INFO, "boot reserve NVRAM range: phys base %#" PRIx64 " length %#" PRIx64 "\n",
            nvram.base, nvram.length);
    allocate_persistent_ram(nvram.base, nvram.length);
    boot_reserve_add_range(nvram.base, nvram.length);
  }

  process_mem_ranges(gPhysHandoff->mem_config.get());
}

void platform_early_init(void) {
  // initialize the boot memory reservation system
  boot_reserve_init();

  ProcessPhysHandoff();

  // is the cmdline option to bypass dlog set ?
  dlog_bypass_init();

  // Serial port should be active now

  // Check if serial should be enabled (i.e., not using the null driver).
  ktl::visit([](const auto& uart) { uart_disabled = uart.extra() == 0; }, gBootOptions->serial);

  // Initialize the PmmChecker now that the cmdline has been parsed.
  pmm_checker_init_from_cmdline();

  // Add the data ZBI ramdisk to the boot reserve memory list.
  ktl::span zbi = ZbiInPhysmap();
  paddr_t ramdisk_start_phys = physmap_to_paddr(zbi.data());
  paddr_t ramdisk_end_phys = ramdisk_start_phys + ROUNDUP_PAGE_SIZE(zbi.size_bytes());
  dprintf(INFO, "reserving ramdisk phys range [%#" PRIx64 ", %#" PRIx64 "]\n", ramdisk_start_phys,
          ramdisk_end_phys - 1);
  boot_reserve_add_range(ramdisk_start_phys, ramdisk_end_phys - ramdisk_start_phys);

  // check if a memory limit was passed in via kernel.memory-limit-mb and
  // find memory ranges to use if one is found.
  zx_status_t status = memory_limit_init();
  bool have_limit = (status == ZX_OK);
  for (size_t i = 0; i < arena_count; i++) {
    if (have_limit) {
      // Figure out and add arenas based on the memory limit and our range of DRAM
      status = memory_limit_add_range(mem_arena[i].base, mem_arena[i].size, mem_arena[i]);
    }

    // If no memory limit was found, or adding arenas from the range failed, then add
    // the existing global arena.
    if (!have_limit || status != ZX_OK) {
      // Init returns not supported if no limit exists
      if (status != ZX_ERR_NOT_SUPPORTED) {
        dprintf(INFO, "memory limit lib returned an error (%d), falling back to defaults\n",
                status);
      }
      pmm_add_arena(&mem_arena[i]);
    }
  }

  // add any pending memory arenas the memory limit library has pending
  if (have_limit) {
    status = memory_limit_add_arenas(mem_arena[0]);
    DEBUG_ASSERT(status == ZX_OK);
  }

  // tell the boot allocator to mark ranges we've reserved as off limits
  boot_reserve_wire();
}

void platform_prevm_init() {}

void platform_init(void) { topology_cpu_init(); }

// after the fact create a region to reserve the peripheral map(s)
static void platform_init_postvm(uint level) { reserve_periph_ranges(); }

LK_INIT_HOOK(platform_postvm, platform_init_postvm, LK_INIT_LEVEL_VM)

zx_status_t platform_mp_prep_cpu_unplug(cpu_num_t cpu_id) {
  return arch_mp_prep_cpu_unplug(cpu_id);
}

zx_status_t platform_mp_cpu_unplug(cpu_num_t cpu_id) { return arch_mp_cpu_unplug(cpu_id); }

void platform_dputs_thread(const char* str, size_t len) {
  if (uart_disabled) {
    return;
  }
  uart_puts(str, len, true);
}

void platform_dputs_irq(const char* str, size_t len) {
  if (uart_disabled) {
    return;
  }
  uart_puts(str, len, false);
}

int platform_dgetc(char* c, bool wait) {
  if (uart_disabled) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  int ret = uart_getc(wait);
  if (ret >= 0) {
    *c = static_cast<char>(ret);
    return 1;
  }
  if (ret == ZX_ERR_SHOULD_WAIT) {
    return 0;
  }
  return ret;
}

void platform_pputc(char c) {
  if (uart_disabled) {
    return;
  }
  uart_pputc(c);
}

int platform_pgetc(char* c) {
  if (uart_disabled) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  int r = uart_pgetc();
  if (r < 0) {
    return r;
  }

  *c = static_cast<char>(r);
  return 0;
}

/* no built in framebuffer */
zx_status_t display_get_info(display_info* info) { return ZX_ERR_NOT_FOUND; }

void platform_specific_halt(platform_halt_action suggested_action, zircon_crash_reason_t reason,
                            bool halt_on_panic) {
  if (suggested_action == HALT_ACTION_REBOOT) {
    power_reboot(REBOOT_NORMAL);
    printf("reboot failed\n");
  } else if (suggested_action == HALT_ACTION_REBOOT_BOOTLOADER) {
    power_reboot(REBOOT_BOOTLOADER);
    printf("reboot-bootloader failed\n");
  } else if (suggested_action == HALT_ACTION_REBOOT_RECOVERY) {
    power_reboot(REBOOT_RECOVERY);
    printf("reboot-recovery failed\n");
  } else if (suggested_action == HALT_ACTION_SHUTDOWN) {
    power_shutdown();
  }

  if (reason == ZirconCrashReason::Panic) {
    Backtrace bt;
    Thread::Current::GetBacktrace(bt);
    bt.Print();
    if (!halt_on_panic) {
      power_reboot(REBOOT_NORMAL);
      printf("reboot failed\n");
    }
#if ENABLE_PANIC_SHELL
    dprintf(ALWAYS, "CRASH: starting debug shell... (reason = %d)\n", static_cast<int>(reason));
    arch_disable_ints();
    panic_shell_start();
#endif  // ENABLE_PANIC_SHELL
  }

  dprintf(ALWAYS, "HALT: spinning forever... (reason = %d)\n", static_cast<int>(reason));

  // catch all fallthrough cases
  arch_disable_ints();

  for (;;) {
    __wfi();
  }
}

void platform_mexec_prep(uintptr_t new_bootimage_addr, size_t new_bootimage_len) {
  DEBUG_ASSERT(!arch_ints_disabled());
  DEBUG_ASSERT(mp_get_online_mask() == cpu_num_to_mask(BOOT_CPU_ID));
}

// This function requires NO_ASAN because it accesses ops, which is memory
// that lives outside of the kernel address space (comes from IdAllocator).
NO_ASAN void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops,
                            uintptr_t new_bootimage_addr, size_t new_bootimage_len,
                            uintptr_t entry64_addr) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(mp_get_online_mask() == cpu_num_to_mask(BOOT_CPU_ID));

  paddr_t kernel_src_phys = (paddr_t)ops[0].src;
  paddr_t kernel_dst_phys = (paddr_t)ops[0].dst;

  // check to see if the kernel is packaged as a zbi container
  zbi_header_t* header = (zbi_header_t*)paddr_to_physmap(kernel_src_phys);
  if (header[0].type == ZBI_TYPE_CONTAINER && header[1].type == ZBI_TYPE_KERNEL_ARM64) {
    zbi_kernel_t* kernel_header = (zbi_kernel_t*)&header[2];
    // add offset from kernel header to entry point
    kernel_dst_phys += kernel_header->entry;
  }
  // else just jump to beginning of kernel image

  mexec_assembly((uintptr_t)new_bootimage_addr, 0, 0, arm64_get_boot_el(), ops,
                 (void*)kernel_dst_phys);
}

bool platform_serial_enabled(void) { return !uart_disabled && uart_present(); }

bool platform_early_console_enabled() { return false; }

// Initialize Resource system after the heap is initialized.
static void arm_resource_dispatcher_init_hook(unsigned int rl) {
  // 64 bit address space for MMIO on ARM64
  zx_status_t status = ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_MMIO, 0, UINT64_MAX);
  if (status != ZX_OK) {
    printf("Resources: Failed to initialize MMIO allocator: %d\n", status);
  }
  // Set up IRQs based on values from the GIC
  status = ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_IRQ, interrupt_get_base_vector(),
                                                   interrupt_get_max_vector());
  if (status != ZX_OK) {
    printf("Resources: Failed to initialize IRQ allocator: %d\n", status);
  }
  // Set up SMC valid service call range
  status = ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_SMC, 0,
                                                   ARM_SMC_SERVICE_CALL_NUM_MAX + 1);
  if (status != ZX_OK) {
    printf("Resources: Failed to initialize SMC allocator: %d\n", status);
  }
  // Set up range of valid system resources.
  status = ResourceDispatcher::InitializeAllocator(ZX_RSRC_KIND_SYSTEM, 0, ZX_RSRC_SYSTEM_COUNT);
  if (status != ZX_OK) {
    printf("Resources: Failed to initialize system allocator: %d\n", status);
  }
}
LK_INIT_HOOK(arm_resource_init, arm_resource_dispatcher_init_hook, LK_INIT_LEVEL_HEAP)

void topology_init() {
  // Check MPIDR_EL1.MT to determine how to interpret AFF0 (i.e. cpu_id). For
  // now, assume that MT is set consistently across all PEs in the system. When
  // MT is set, use the next affinity level for the first cache depth element.
  // This approach should be adjusted if we find examples of systems that do not
  // set MT uniformly, and may require delaying cache-aware load balancing until
  // all PEs are initialized.
  const bool cpu_id_is_thread_id = __arm_rsr64("mpidr_el1") & (1 << 24);
  printf("topology_init: MPIDR_EL1.MT=%d\n", cpu_id_is_thread_id);

  // This platform initializes the topology earlier than this standard hook.
  // Setup the CPU distance map with the already initialized topology.
  const auto processor_count =
      static_cast<uint>(system_topology::GetSystemTopology().processor_count());
  CpuDistanceMap::Initialize(processor_count, [cpu_id_is_thread_id](cpu_num_t from_id,
                                                                    cpu_num_t to_id) {
    using system_topology::Node;
    using system_topology::Graph;

    const Graph& topology = system_topology::GetSystemTopology();

    Node* from_node = nullptr;
    if (topology.ProcessorByLogicalId(from_id, &from_node) != ZX_OK) {
      printf("Failed to get processor node for CPU %u\n", from_id);
      return -1;
    }
    DEBUG_ASSERT(from_node != nullptr);

    Node* to_node = nullptr;
    if (topology.ProcessorByLogicalId(to_id, &to_node) != ZX_OK) {
      printf("Failed to get processor node for CPU %u\n", to_id);
      return -1;
    }
    DEBUG_ASSERT(to_node != nullptr);

    const zbi_topology_arm_info_t& from_info = from_node->entity.processor.architecture_info.arm;
    const zbi_topology_arm_info_t& to_info = to_node->entity.processor.architecture_info.arm;

    // Return the maximum cache depth not shared when multithreaded.
    if (cpu_id_is_thread_id) {
      return ktl::max({1 * int{from_info.cluster_1_id != to_info.cluster_1_id},
                       2 * int{from_info.cluster_2_id != to_info.cluster_2_id},
                       3 * int{from_info.cluster_3_id != to_info.cluster_3_id}});
    }

    // Return the maximum cache depth not shared when single threaded.
    return ktl::max({1 * int{from_info.cpu_id != to_info.cpu_id},
                     2 * int{from_info.cluster_1_id != to_info.cluster_1_id},
                     3 * int{from_info.cluster_2_id != to_info.cluster_2_id},
                     4 * int{from_info.cluster_3_id != to_info.cluster_3_id}});
  });

  // TODO(eieio): Determine automatically or provide a way to specify in the
  // ZBI. The current value matches the depth of the first significant cache
  // above.
  const CpuDistanceMap::Distance kDistanceThreshold = 2u;
  CpuDistanceMap::Get().set_distance_threshold(kDistanceThreshold);

  CpuDistanceMap::Get().Dump();
}
