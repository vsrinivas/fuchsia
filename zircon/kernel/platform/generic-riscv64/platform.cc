// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch.h>
#include <debug.h>
#include <lib/affine/ratio.h>
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
#include <arch/riscv64/sbi.h>
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
#include <platform/timer.h>
#include <vm/bootreserve.h>
#include <vm/kstack.h>
#include <vm/physmap.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>
#if WITH_PANIC_BACKTRACE
#include <kernel/thread.h>
#endif

#include <lib/zbitl/error_stdio.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/memory.h>
#include <zircon/boot/image.h>
#include <zircon/errors.h>
#include <zircon/rights.h>
#include <zircon/syscalls/smc.h>
#include <zircon/types.h>

#include <pdev/pdev.h>

#define LOCAL_TRACE 0

// Defined in start.S.
extern paddr_t zbi_paddr;

static void* ramdisk_base;
static size_t ramdisk_size;

static zbi_header_t* zbi_root = nullptr;

static bool uart_disabled = false;

// all of the configured memory arenas from the zbi
static constexpr size_t kNumArenas = 16;
static pmm_arena_info_t mem_arena[kNumArenas];
static size_t arena_count = 0;

const zbi_header_t* platform_get_zbi(void) { return zbi_root; }

void platform_panic_start(void) {
  static ktl::atomic<int> panic_started;

  arch_disable_ints();


  if (panic_started.exchange(1) == 0) {
    dlog_bluescreen_init();
  }
}

void* platform_get_ramdisk(size_t* size) {
  if (ramdisk_base) {
    *size = ramdisk_size;
    return ramdisk_base;
  } else {
    *size = 0;
    return nullptr;
  }
}

void platform_halt_cpu(void) {
}

static inline bool is_zbi_container(void* addr) {
  DEBUG_ASSERT(addr);

  zbi_header_t* item = (zbi_header_t*)addr;
  return item->type == ZBI_TYPE_CONTAINER;
}

static void process_mem_range(const zbi_mem_range_t* mem_range) {
  switch (mem_range->type) {
    case ZBI_MEM_RANGE_RAM:
      dprintf(INFO, "ZBI: mem arena base %#" PRIx64 " size %#" PRIx64 "\n", mem_range->paddr,
              mem_range->length);
      if (arena_count >= kNumArenas) {
        printf("ZBI: Warning, too many memory arenas, dropping additional\n");
        break;
      }
      mem_arena[arena_count] = pmm_arena_info_t{"ram", 0, mem_range->paddr, mem_range->length};
      arena_count++;
      break;
    case ZBI_MEM_RANGE_PERIPHERAL: {
      dprintf(INFO, "ZBI: peripheral range base %#" PRIx64 " size %#" PRIx64 "\n", mem_range->paddr,
              mem_range->length);
//      auto status = add_periph_range(mem_range->paddr, mem_range->length);
//      ASSERT(status == ZX_OK);
      break;
    }
    case ZBI_MEM_RANGE_RESERVED:
      dprintf(INFO, "ZBI: reserve mem range base %#" PRIx64 " size %#" PRIx64 "\n",
              mem_range->paddr, mem_range->length);
      boot_reserve_add_range(mem_range->paddr, mem_range->length);
      break;
    default:
      panic("bad mem_range->type in process_mem_range\n");
      break;
  }
}

static constexpr zbi_topology_node_t fallback_topology = {
    .entity_type = ZBI_TOPOLOGY_ENTITY_PROCESSOR,
    .parent_index = ZBI_TOPOLOGY_NO_PARENT,
    .entity = {.processor = {.logical_ids = {0},
                             .logical_id_count = 1,
                             .flags = 0,
                             .architecture = ZBI_TOPOLOGY_ARCH_RISCV}}};

static void init_topology(const zbi_topology_node_t* nodes, size_t node_count) {
  auto result = system_topology::Graph::InitializeSystemTopology(nodes, node_count);
  if (result != ZX_OK) {
    printf("Failed to initialize system topology! error: %d\n", result);

    // Try to fallback to a topology of just this processor.
    result = system_topology::Graph::InitializeSystemTopology(&fallback_topology, 1);
    ASSERT(result == ZX_OK);
  }

  arch_set_num_cpus(static_cast<uint>(system_topology::GetSystemTopology().processor_count()));
}

// Called during platform_init_early, the heap is not yet present.
void ProcessZbiEarly(zbi_header_t* zbi) {
  DEBUG_ASSERT(zbi);

  // Writable bytes, as we will need to edit CMDLINE items (see below).
  zbitl::View view(zbitl::AsWritableBytes(zbi, SIZE_MAX));

  for (auto it = view.begin(); it != view.end(); ++it) {
    auto [header, payload] = *it;
    switch (header->type) {
      case ZBI_TYPE_KERNEL_DRIVER:
      case ZBI_TYPE_PLATFORM_ID:
        break;
      case ZBI_TYPE_CMDLINE: {
        if (payload.empty()) {
          break;
        }
        payload.back() = std::byte{'\0'};
        gCmdline.Append(reinterpret_cast<const char*>(payload.data()));

        // The CMDLINE might include entropy for the zircon cprng.
        // We don't want that information to be accesible after it has
        // been added to the kernel cmdline.
        // Editing the header of a ktl::span will not result in an error.
        // TODO(fxbug.dev/64272): Inline the following once the GCC bug is fixed.
        zbi_header_t header{};
        header.type = ZBI_TYPE_DISCARD;
        static_cast<void>(view.EditHeader(it, header));
        mandatory_memset(payload.data(), 0, payload.size());
        break;
      }
      case ZBI_TYPE_MEM_CONFIG: {
        zbi_mem_range_t* mem_range = reinterpret_cast<zbi_mem_range_t*>(payload.data());
        size_t count = payload.size() / sizeof(zbi_mem_range_t);
        for (size_t i = 0; i < count; i++) {
          process_mem_range(mem_range++);
        }
        break;
      }
      case ZBI_TYPE_NVRAM: {
        zbi_nvram_t info;
        memcpy(&info, payload.data(), sizeof(info));

        dprintf(INFO, "boot reserve NVRAM range: phys base %#" PRIx64 " length %#" PRIx64 "\n",
                info.base, info.length);

        platform_set_ram_crashlog_location(info.base, info.length);
        boot_reserve_add_range(info.base, info.length);
        break;
      }
      case ZBI_TYPE_HW_REBOOT_REASON: {
        zbi_hw_reboot_reason_t reason;
        memcpy(&reason, payload.data(), sizeof(reason));
        platform_set_hw_reboot_reason(reason);
        break;
      }
    };
  }

  if (auto result = view.take_error(); result.is_error()) {
    printf("ProcessZbiEarly: encountered error iterating through data ZBI: ");
    zbitl::PrintViewError(result.error_value());
  }
}

// Called after the heap is up, but before multithreading.
void ProcessZbiLate(const zbi_header_t* zbi) {
  DEBUG_ASSERT(zbi);

  zbitl::View view(zbitl::AsBytes(zbi, SIZE_MAX));

  for (auto it = view.begin(); it != view.end(); ++it) {
    auto [header, payload] = *it;
    switch (header->type) {
      case ZBI_TYPE_CPU_TOPOLOGY: {
        const size_t node_count = payload.size() / static_cast<size_t>(header->extra);
        const auto* nodes = reinterpret_cast<const zbi_topology_node_t*>(payload.data());
        init_topology(nodes, node_count);
        break;
      }
    };
  }

  if (auto result = view.take_error(); result.is_error()) {
    printf("ProcessZbiLate: encountered error iterating through data ZBI: ");
    zbitl::PrintViewError(result.error_value());
  }
}

void platform_early_init(void) {
  // if the zbi_paddr variable is -1, it was not set
  // in start.S, so we are in a bad place.
  if (zbi_paddr == -1UL) {
    panic("no zbi_paddr!\n");
  }

  void* zbi_vaddr = paddr_to_physmap(zbi_paddr);

  // initialize the boot memory reservation system
  boot_reserve_init();

  if (zbi_vaddr && is_zbi_container(zbi_vaddr)) {
    zbi_header_t* header = (zbi_header_t*)zbi_vaddr;


    ramdisk_base = header;
    ramdisk_size = ROUNDUP(header->length + sizeof(*header), PAGE_SIZE);
  } else {
    panic("no bootdata!\n");
  }

  if (!ramdisk_base || !ramdisk_size) {
    panic("no ramdisk!\n");
  }

  zbi_root = reinterpret_cast<zbi_header_t*>(ramdisk_base);
  // walk the zbi structure and process all the items
  ProcessZbiEarly(zbi_root);

  // is the cmdline option to bypass dlog set ?
  dlog_bypass_init();

  // bring up kernel drivers after we have mapped our peripheral ranges
  pdev_init(zbi_root);

  // Serial port should be active now

  // Check if serial should be enabled
  const char* serial_mode = gCmdline.GetString("kernel.serial");
  uart_disabled = (serial_mode != NULL && !strcmp(serial_mode, "none"));

  // Initialize the PmmChecker now that the cmdline has been parsed.
  pmm_checker_init_from_cmdline();

  // add the ramdisk to the boot reserve memory list
  paddr_t ramdisk_start_phys = physmap_to_paddr(ramdisk_base);
  paddr_t ramdisk_end_phys = ramdisk_start_phys + ramdisk_size;
  dprintf(INFO, "reserving ramdisk phys range [%#" PRIx64 ", %#" PRIx64 "]\n", ramdisk_start_phys,
          ramdisk_end_phys - 1);
  boot_reserve_add_range(ramdisk_start_phys, ramdisk_size);

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

    if (strcmp(mem_arena[i].name, "ram") == 0) {
      // reserve the first 128K of ram, marked protected by the PMP in firmware
      struct list_node list = LIST_INITIAL_VALUE(list);
      pmm_alloc_range(mem_arena[i].base, 0x20000 / PAGE_SIZE, &list);
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

// Called after the heap is up but before the system is multithreaded.
void platform_init_pre_thread(uint) { ProcessZbiLate(zbi_root); }

LK_INIT_HOOK(platform_init_pre_thread, platform_init_pre_thread, LK_INIT_LEVEL_VM)

void platform_init(void) { }

// after the fact create a region to reserve the peripheral map(s)
static void platform_init_postvm(uint level) { }

LK_INIT_HOOK(platform_postvm, platform_init_postvm, LK_INIT_LEVEL_VM)

void platform_dputs_thread(const char* str, size_t len) {
  if (uart_disabled) {
    return;
  }
  uart_puts(str, len, true, true);
}

void platform_dputs_irq(const char* str, size_t len) {
  if (uart_disabled) {
    return;
  }
  uart_puts(str, len, false, true);
}

int platform_dgetc(char* c, bool wait) {
  if (uart_disabled) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  int ret = uart_getc(wait);
  // uart_getc returns ZX_ERR_INTERNAL if no input was read
  if (!wait && ret == ZX_ERR_INTERNAL)
    return 0;
  if (ret < 0)
    return ret;
  *c = static_cast<char>(ret);
  return 1;
}

void platform_pputc(char c) {
  if (uart_disabled) {
    return;
  }
  uart_pputc(c);
}

int platform_pgetc(char* c, bool wait) {
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
zx_status_t display_get_info(struct display_info* info) { return ZX_ERR_NOT_FOUND; }

void platform_specific_halt(platform_halt_action suggested_action, zircon_crash_reason_t reason,
                            bool halt_on_panic) {
  sbi_call(SBI_SHUTDOWN);
  __UNREACHABLE;
}

zx_status_t platform_mexec_patch_zbi(uint8_t* zbi, const size_t len) {
  return ZX_OK;
}

void platform_mexec_prep(uintptr_t new_bootimage_addr, size_t new_bootimage_len) {
}

void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops, uintptr_t new_bootimage_addr,
                    size_t new_bootimage_len, uintptr_t entry64_addr) {
}

bool platform_serial_enabled(void) { return !uart_disabled; }

bool platform_early_console_enabled() { return false; }

// Initialize Resource system after the heap is initialized.
static void riscv64_resource_dispatcher_init_hook(unsigned int rl) {
}

LK_INIT_HOOK(riscv64_resource_init, riscv64_resource_dispatcher_init_hook, LK_INIT_LEVEL_HEAP)

void topology_init() {
}

zx_status_t platform_mp_prep_cpu_unplug(cpu_num_t cpu_id) {
  return arch_mp_prep_cpu_unplug(cpu_id);
}

zx_status_t platform_mp_cpu_unplug(cpu_num_t cpu_id) { return arch_mp_cpu_unplug(cpu_id); }

zx_status_t platform_append_mexec_data(fbl::Span<std::byte> data_zbi)  {
  return ZX_OK;
}
