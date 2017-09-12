// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <err.h>
#include <fbl/auto_lock.h>
#include <fbl/atomic.h>
#include <fbl/ref_ptr.h>
#include <reg.h>
#include <trace.h>

#include <dev/uart.h>
#include <arch.h>
#include <lk/init.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>
#include <kernel/spinlock.h>
#include <dev/display.h>
#include <dev/hw_rng.h>
#include <dev/psci.h>

#include <platform.h>
#include <mexec.h>
#include <dev/bcm28xx.h>

#include <target.h>

#include <arch/efi.h>
#include <arch/mp.h>
#include <arch/arm64/mp.h>
#include <arch/arm64.h>
#include <arch/arm64/mmu.h>

#include <vm/initial_map.h>
#include <vm/vm_aspace.h>

#include <lib/console.h>
#include <lib/memory_limit.h>
#if WITH_LIB_DEBUGLOG
#include <lib/debuglog.h>
#endif
#if WITH_PANIC_BACKTRACE
#include <kernel/thread.h>
#endif

#include <magenta/boot/bootdata.h>
#include <mdi/mdi.h>
#include <mdi/mdi-defs.h>
#include <pdev/pdev.h>
#include <libfdt.h>

extern paddr_t boot_structure_paddr; // Defined in start.S.

static paddr_t ramdisk_start_phys = 0;
static paddr_t ramdisk_end_phys = 0;

static void* ramdisk_base;
static size_t ramdisk_size;

static uint cpu_cluster_count = 0;
static uint cpu_cluster_cpus[SMP_CPU_MAX_CLUSTERS] = {0};

static bool halt_on_panic = false;

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
 /* 1GB of sdram space */
 {
     .phys = MEMBASE,
     .virt = KERNEL_BASE,
     .size = MEMORY_APERTURE_SIZE,
     .flags = 0,
     .name = "memory"
 },

 /* peripherals */
 {
     .phys = PERIPH_BASE_PHYS,
     .virt = PERIPH_BASE_VIRT,
     .size = PERIPH_SIZE,
     .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
     .name = "peripherals"
 },
 /* null entry to terminate the list */
 {}
};

static pmm_arena_info_t arena = {
    /* .name */     "sdram",
    /* .flags */    PMM_ARENA_FLAG_KMAP,
    /* .priority */ 0,
    /* .base */     MEMBASE,
    /* .size */     MEMSIZE,
};

static volatile int panic_started;

static void halt_other_cpus(void)
{
    static volatile int halted = 0;

    if (atomic_swap(&halted, 1) == 0) {
        // stop the other cpus
        printf("stopping other cpus\n");
        arch_mp_send_ipi(MP_IPI_TARGET_ALL_BUT_LOCAL, 0, MP_IPI_HALT);

        // spin for a while
        // TODO: find a better way to spin at this low level
        for (volatile int i = 0; i < 100000000; i++) {
            __asm volatile ("nop");
        }
    }
}

void platform_panic_start(void)
{
    arch_disable_ints();

    halt_other_cpus();

    if (atomic_swap(&panic_started, 1) == 0) {
#if WITH_LIB_DEBUGLOG
        dlog_bluescreen_init();
#endif
    }
}

// Reads Linux device tree to initialize command line and return ramdisk location
static void read_device_tree(void** ramdisk_base, size_t* ramdisk_size, size_t* mem_size) {
    if (ramdisk_base) *ramdisk_base = nullptr;
    if (ramdisk_size) *ramdisk_size = 0;
    if (mem_size) *mem_size = 0;

    void* fdt = paddr_to_kvaddr(boot_structure_paddr);
    if (!fdt) {
        printf("%s: could not find device tree\n", __FUNCTION__);
        return;
    }

    if (fdt_check_header(fdt) < 0) {
        printf("%s fdt_check_header failed\n", __FUNCTION__);
        return;
    }

    int offset = fdt_path_offset(fdt, "/chosen");
    if (offset < 0) {
        printf("%s: fdt_path_offset(/chosen) failed\n", __FUNCTION__);
        return;
    }

    int length;
    const char* bootargs =
        static_cast<const char*>(fdt_getprop(fdt, offset, "bootargs", &length));
    if (bootargs) {
        printf("kernel command line: %s\n", bootargs);
        cmdline_append(bootargs);
    }

    if (ramdisk_base && ramdisk_size) {
        const void* ptr = fdt_getprop(fdt, offset, "linux,initrd-start", &length);
        if (ptr) {
            if (length == 4) {
                ramdisk_start_phys = fdt32_to_cpu(*(uint32_t *)ptr);
            } else if (length == 8) {
                ramdisk_start_phys = fdt64_to_cpu(*(uint64_t *)ptr);
            }
        }
        ptr = fdt_getprop(fdt, offset, "linux,initrd-end", &length);
        if (ptr) {
            if (length == 4) {
                ramdisk_end_phys = fdt32_to_cpu(*(uint32_t *)ptr);
            } else if (length == 8) {
                ramdisk_end_phys = fdt64_to_cpu(*(uint64_t *)ptr);
            }
        }
        // Some bootloaders pass initrd via cmdline, lets look there
        //  if we haven't found it yet.
        if (!(ramdisk_start_phys && ramdisk_end_phys)) {
            const char* value = cmdline_get("initrd");
            if (value != NULL) {
                char* endptr;
                ramdisk_start_phys = strtoll(value,&endptr,16);
                endptr++; //skip the comma
                ramdisk_end_phys = strtoll(endptr,NULL,16) + ramdisk_start_phys;
            }
        }

        if (ramdisk_start_phys && ramdisk_end_phys) {
            *ramdisk_base = paddr_to_kvaddr(ramdisk_start_phys);
            size_t length = ramdisk_end_phys - ramdisk_start_phys;
            *ramdisk_size = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        }
    }

    // look for memory size. currently only used for qemu build
    if (mem_size) {
        offset = fdt_path_offset(fdt, "/memory");
        if (offset < 0) {
            printf("%s: fdt_path_offset(/memory) failed\n", __FUNCTION__);
            return;
        }
        int lenp;
        const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
        if (prop_ptr && lenp == 0x10) {
            /* we're looking at a memory descriptor */
            //uint64_t base = fdt64_to_cpu(*(uint64_t *)prop_ptr);
            *mem_size = fdt64_to_cpu(*((const uint64_t *)prop_ptr + 1));
        }
    }
}

static void platform_preserve_ramdisk(void) {
    if (!ramdisk_start_phys || !ramdisk_end_phys) {
        return;
    }

    struct list_node list = LIST_INITIAL_VALUE(list);
    size_t pages = (ramdisk_end_phys - ramdisk_start_phys + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t actual = pmm_alloc_range(ramdisk_start_phys, pages, &list);
    if (actual != pages) {
        panic("unable to reserve ramdisk memory range\n");
    }

    // mark all of the pages we allocated as WIRED
    vm_page_t *p;
    list_for_every_entry(&list, p, vm_page_t, free.node) {
        p->state = VM_PAGE_STATE_WIRED;
    }
}

void* platform_get_ramdisk(size_t *size) {
    if (ramdisk_base) {
        *size = ramdisk_size;
        return ramdisk_base;
    } else {
        *size = 0;
        return nullptr;
    }
}

static void platform_cpu_early_init(mdi_node_ref_t* cpu_map) {
    mdi_node_ref_t  clusters;

    if (mdi_find_node(cpu_map, MDI_CPU_CLUSTERS, &clusters) != MX_OK) {
        panic("platform_cpu_early_init couldn't find clusters\n");
        return;
    }

    mdi_node_ref_t  cluster;

    mdi_each_child(&clusters, &cluster) {
        mdi_node_ref_t node;
        uint8_t cpu_count;

        if (mdi_find_node(&cluster, MDI_CPU_COUNT, &node) != MX_OK) {
            panic("platform_cpu_early_init couldn't find cluster cpu-count\n");
            return;
        }
        if (mdi_node_uint8(&node, &cpu_count) != MX_OK) {
            panic("platform_cpu_early_init could not read cluster id\n");
            return;
        }

        if (cpu_cluster_count >= SMP_CPU_MAX_CLUSTERS) {
            panic("platform_cpu_early_init: MDI contains more than SMP_CPU_MAX_CLUSTERS clusters\n");
            return;
        }
        cpu_cluster_cpus[cpu_cluster_count++] = cpu_count;
    }
    arch_init_cpu_map(cpu_cluster_count, cpu_cluster_cpus);
}


#if BCM2837

#define BCM2837_CPU_SPIN_TABLE_ADDR   0xd8

// Make sure that the KERNEL_SPIN_OFFSET is completely clear of the Spin table
// since we don't want to overwrite the spin vectors.
#define KERNEL_SPIN_OFFSET (ROUNDUP(BCM2837_CPU_SPIN_TABLE_ADDR +              \
                            (sizeof(uintptr_t) * SMP_MAX_CPUS), CACHE_LINE))

// Prototype of assembly function where the CPU will be parked.
typedef void (*park_cpu)(uint32_t cpuid, uintptr_t spin_table_addr);

// Implemented in Assembly.
__BEGIN_CDECLS
extern void bcm28xx_park_cpu(void);
extern void bcm28xx_park_cpu_end(void);
__END_CDECLS

// The first CPU to halt will setup the halt_aspace and map a WFE spin loop into
// the halt aspace.
// Subsequent CPUs will reuse this aspace and mapping.
static fbl::Mutex cpu_halt_lock;
static fbl::RefPtr<VmAspace> halt_aspace = nullptr;
static bool mapped_boot_pages = false;

void platform_halt_cpu(void) {
    status_t result;
    park_cpu park = (park_cpu)KERNEL_SPIN_OFFSET;
    thread_t *self = get_current_thread();
    const uint cpuid = thread_last_cpu(self);

    fbl::AutoLock lock(&cpu_halt_lock);
    // If we're the first CPU to halt then we need to create an address space to
    // park the CPUs in. Any subsequent calls to platform_halt_cpu will also
    // share this address space.
    if (!halt_aspace) {
        halt_aspace = VmAspace::Create(VmAspace::TYPE_LOW_KERNEL, "halt_cpu");
        if (!halt_aspace) {
            printf("failed to create halt_cpu vm aspace\n");
            return;
        }
    }

    // Create an identity mapped page at the base of RAM. This is where the
    // BCM28xx puts its bootcode.
    if (!mapped_boot_pages) {
        paddr_t pa = 0;
        void* base_of_ram = nullptr;
        const uint perm_flags_rwx = ARCH_MMU_FLAG_PERM_READ  |
                                    ARCH_MMU_FLAG_PERM_WRITE |
                                    ARCH_MMU_FLAG_PERM_EXECUTE;

        // Map a page in this ASpace at address 0, where we'll be parking
        // the core after it halts.
        result = halt_aspace->AllocPhysical("halt_mapping", PAGE_SIZE,
                                            &base_of_ram, 0, pa,
                                            VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
                                            perm_flags_rwx);

        if (result != MX_OK) {
            printf("Unable to allocate physical at vaddr = %p, paddr = %p\n",
                   base_of_ram, (void*)pa);
            return;
        }

        // Copy the spin loop into the base of RAM. This is where we will park
        // the CPU.
        size_t bcm28xx_park_cpu_length = (uintptr_t)bcm28xx_park_cpu_end -
                                         (uintptr_t)bcm28xx_park_cpu;

        // Make sure the assembly for the CPU spin loop fits within the
        // page that we allocated.
        DEBUG_ASSERT((bcm28xx_park_cpu_length + KERNEL_SPIN_OFFSET) < PAGE_SIZE);

        memcpy((void*)(KERNEL_ASPACE_BASE + KERNEL_SPIN_OFFSET),
               reinterpret_cast<const void*>(bcm28xx_park_cpu),
               bcm28xx_park_cpu_length);

        fbl::atomic_signal_fence();
        arch_clean_cache_range(KERNEL_ASPACE_BASE, 4096);     // clean out all the VC bootstrap area
        arch_sync_cache_range(KERNEL_ASPACE_BASE, 4096);     // clean out all the VC bootstrap area


        // Only the first core that calls this method needs to setup the address
        // space and load the bootcode into the base of RAM so once this call
        // succeeds all subsequent cores can simply use what was provided by
        // the first core.
        mapped_boot_pages = true;
    }

    vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(halt_aspace.get()));

    lock.release();

    mp_set_curr_cpu_active(false);
    mp_set_curr_cpu_online(false);

    park(cpuid, 0xd8);

    panic("control should never reach here");
}

#else

void platform_halt_cpu(void) {
    psci_cpu_off();
}

#endif

// One of these threads is spun up per CPU and calls halt which does not return.
static int park_cpu_thread(void* arg) {
    // Make sure we're not lopping off the top bits of the arg
    DEBUG_ASSERT(((uintptr_t)arg & 0xffffffff00000000) == 0);
    uint32_t cpu_id = (uint32_t)((uintptr_t)arg & 0xffffffff);

    // From hereon in, this thread will always be assigned to the pinned cpu.
    thread_migrate_cpu(cpu_id);

    arch_disable_ints();

    // This method will not return because the target cpu has halted.
    platform_halt_cpu();

    panic("control should never reach here");
    return -1;
}

void platform_halt_secondary_cpus(void) {
    // Create one thread per core to park each core.
    thread_t** park_thread =
        (thread_t**)calloc(arch_max_num_cpus(), sizeof(*park_thread));
    for (uint i = 0; i < arch_max_num_cpus(); i++) {
        // The boot cpu is going to be performing the remainder of the mexec
        // for us so we don't want to park that one.
        if (i == BOOT_CPU_ID) {
            continue;
        }

        char park_thread_name[20];
        snprintf(park_thread_name, sizeof(park_thread_name), "park %u", i);
        park_thread[i] = thread_create(park_thread_name, park_cpu_thread,
                                       (void*)(uintptr_t)i, DEFAULT_PRIORITY,
                                       DEFAULT_STACK_SIZE);
        thread_resume(park_thread[i]);
    }

    // TODO(gkalsi): Wait for the secondaries to shutdown rather than sleeping
    thread_sleep_relative(LK_SEC(2));
}

static void platform_start_cpu(uint cluster, uint cpu) {
#if BCM2837
    uintptr_t sec_entry = reinterpret_cast<uintptr_t>(&arm_reset) - KERNEL_ASPACE_BASE;
    unsigned long long *spin_table =
        reinterpret_cast<unsigned long long *>(KERNEL_ASPACE_BASE + 0xd8);

    spin_table[cpu] = sec_entry;
    __asm__ __volatile__ ("" : : : "memory");
    arch_clean_cache_range(0xffff000000000000,256);     // clean out all the VC bootstrap area
    __asm__ __volatile__("sev");                        //  where the entry vectors live.
#else
      if (cluster==0)
          printf("Trying to start cpu%u returned: %x\n",cpu, psci_cpu_on(cluster, cpu, MEMBASE + KERNEL_LOAD_OFFSET));
#endif
}

static void* allocate_one_stack(void) {
    uint8_t* stack = static_cast<uint8_t*>(
        pmm_alloc_kpages(ARCH_DEFAULT_STACK_SIZE / PAGE_SIZE, nullptr, nullptr)
    );
    return static_cast<void*>(stack + ARCH_DEFAULT_STACK_SIZE);
}

static void platform_cpu_init(void) {
    for (uint cluster = 0; cluster < cpu_cluster_count; cluster++) {
        for (uint cpu = 0; cpu < cpu_cluster_cpus[cluster]; cpu++) {
            if (cluster != 0 || cpu != 0) {
                void* sp = allocate_one_stack();
                void* unsafe_sp = nullptr;
#if __has_feature(safe_stack)
                unsafe_sp = allocate_one_stack();
#endif
                arm64_set_secondary_sp(cluster, cpu, sp, unsafe_sp);
                platform_start_cpu(cluster, cpu);
            }
        }
    }
}

static inline bool is_magenta_boot_header(void* addr) {
    DEBUG_ASSERT(addr);

    efi_magenta_hdr_t* header = (efi_magenta_hdr_t*)addr;


    return header->magic == EFI_MAGENTA_MAGIC;
}

static inline bool is_bootdata_container(void* addr) {
    DEBUG_ASSERT(addr);

    bootdata_t* header = (bootdata_t*)addr;

    return header->type == BOOTDATA_CONTAINER;
}

static void ramdisk_from_bootdata_container(void* bootdata,
                                            void** ramdisk_base,
                                            size_t* ramdisk_size) {
    bootdata_t* header = (bootdata_t*)bootdata;

    DEBUG_ASSERT(header->type == BOOTDATA_CONTAINER);

    *ramdisk_base = (void*)bootdata;
    *ramdisk_size = ROUNDUP(header->length + sizeof(*header), PAGE_SIZE);
}

static void platform_mdi_init(const bootdata_t* section) {
    mdi_node_ref_t  root;
    mdi_node_ref_t  cpu_map;
    mdi_node_ref_t  kernel_drivers;

    const void* ramdisk_end = reinterpret_cast<uint8_t*>(ramdisk_base) + ramdisk_size;
    const void* section_ptr = reinterpret_cast<const void *>(section);
    const size_t length = reinterpret_cast<uintptr_t>(ramdisk_end) - reinterpret_cast<uintptr_t>(section_ptr);

    if (mdi_init(section_ptr, length, &root) != MX_OK) {
        panic("mdi_init failed\n");
    }

    // search top level nodes for CPU info and kernel drivers
    if (mdi_find_node(&root, MDI_CPU_MAP, &cpu_map) != MX_OK) {
        panic("platform_mdi_init couldn't find cpu-map\n");
    }
    if (mdi_find_node(&root, MDI_KERNEL, &kernel_drivers) != MX_OK) {
        panic("platform_mdi_init couldn't find kernel-drivers\n");
    }

    platform_cpu_early_init(&cpu_map);

    pdev_init(&kernel_drivers);
}

static uint32_t process_bootsection(bootdata_t* section, size_t hsz) {
    switch(section->type) {
    case BOOTDATA_MDI:
        platform_mdi_init(section);
        break;
    case BOOTDATA_CMDLINE:
        if (section->length < 1) {
            break;
        }
        char* contents = reinterpret_cast<char*>(section) + hsz;
        contents[section->length - 1] = '\0';
        cmdline_append(contents);
        break;
    }

    return section->type;
}

static void process_bootdata(bootdata_t* root) {
    DEBUG_ASSERT(root);

    if (root->type != BOOTDATA_CONTAINER) {
        printf("bootdata: invalid type = %08x\n", root->type);
        return;
    }

    if (root->extra != BOOTDATA_MAGIC) {
        printf("bootdata: invalid magic = %08x\n", root->extra);
        return;
    }

    bool mdi_found = false;
    size_t offset = sizeof(bootdata_t);
    const size_t length = (root->length);

    if (root->flags & BOOTDATA_FLAG_EXTRA) {
        offset += sizeof(bootextra_t);
    }

    while (offset < length) {

        uintptr_t ptr = reinterpret_cast<const uintptr_t>(root);
        bootdata_t* section = reinterpret_cast<bootdata_t*>(ptr + offset);

        size_t hsz = sizeof(bootdata_t);
        if (section->flags & BOOTDATA_FLAG_EXTRA) {
            hsz += sizeof(bootextra_t);
        }

        const uint32_t type = process_bootsection(section, hsz);
        if (BOOTDATA_MDI == type) {
            mdi_found = true;
        }

        offset += BOOTDATA_ALIGN(hsz + section->length);
    }

    if (!mdi_found) {
        panic("No MDI found in ramdisk\n");
    }
}
extern int _end;
void platform_early_init(void)
{
    // QEMU does not put device tree pointer in the boot-time x2 register,
    // so set it here before calling read_device_tree.
    if (boot_structure_paddr == 0) {
        boot_structure_paddr = MEMBASE;
    }

    void* boot_structure_kvaddr = paddr_to_kvaddr(boot_structure_paddr);
    if (!boot_structure_kvaddr) {
        panic("no bootdata structure!\n");
    }

    // The previous environment passes us a boot structure. It may be a
    // device tree or a bootdata container. We attempt to detect the type of the
    // container and handle it appropriately.
    size_t arena_size = 0;
    if (is_bootdata_container(boot_structure_kvaddr)) {
        // We leave out arena size for now
        ramdisk_from_bootdata_container(boot_structure_kvaddr, &ramdisk_base,
                                        &ramdisk_size);
    } else if (is_magenta_boot_header(boot_structure_kvaddr)) {
            efi_magenta_hdr_t *hdr = (efi_magenta_hdr_t*)boot_structure_kvaddr;
            cmdline_append(hdr->cmd_line);
            ramdisk_start_phys = hdr->ramdisk_base_phys;
            ramdisk_size = hdr->ramdisk_size;
            ramdisk_end_phys = ramdisk_start_phys + ramdisk_size;
            ramdisk_base =  paddr_to_kvaddr(ramdisk_start_phys);
    } else {
        // on qemu we read arena size from the device tree
        read_device_tree(&ramdisk_base, &ramdisk_size, &arena_size);
        // Some legacy bootloaders do not properly set linux,initrd-end
        // Pull the ramdisk size directly from the bootdata container
        //   now that we have the base to ensure that the size is valid.
        ramdisk_from_bootdata_container(ramdisk_base, &ramdisk_base,
                                        &ramdisk_size);
    }

    if (!ramdisk_base || !ramdisk_size) {
        panic("no ramdisk!\n");
    }
    process_bootdata(reinterpret_cast<bootdata_t*>(ramdisk_base));
    // Read cmdline after processing bootdata, which may contain cmdline data.
    halt_on_panic = cmdline_get_bool("kernel.halt-on-panic", false);

    /* add the main memory arena */
    if (arena_size) {
        arena.size = arena_size;
    }

    // check if a memory limit was passed in via kernel.memory-limit-mb and
    // find memory ranges to use if one is found.
    mem_limit_ctx_t ctx;
    status_t status = mem_limit_init(&ctx);
    if (status == MX_OK) {
        // For these ranges we're using the base physical values
        ctx.kernel_base = MEMBASE + KERNEL_LOAD_OFFSET;
        ctx.kernel_size = (uintptr_t)&_end - ctx.kernel_base;
        ctx.ramdisk_base = ramdisk_start_phys;
        ctx.ramdisk_size = ramdisk_end_phys - ramdisk_start_phys;

        // Figure out and add arenas based on the memory limit and our range of DRAM
        status = mem_limit_add_arenas_from_range(&ctx, arena.base, arena.size, arena);
    }

    // If no memory limit was found, or adding arenas from the range failed, then add
    // the existing global arena.
    if (status != MX_OK) {
        pmm_add_arena(&arena);
    }

#ifdef BOOTLOADER_RESERVE_START
    /* Allocate memory regions reserved by bootloaders for other functions */
    pmm_alloc_range(BOOTLOADER_RESERVE_START, BOOTLOADER_RESERVE_SIZE / PAGE_SIZE, nullptr);
#endif

    platform_preserve_ramdisk();
}

void platform_init(void)
{
    platform_cpu_init();
}

void platform_dputs(const char* str, size_t len)
{
    while (len-- > 0) {
        char c = *str++;
        if (c == '\n') {
            uart_putc('\r');
        }
        uart_putc(c);
    }
}

int platform_dgetc(char *c, bool wait)
{
    int ret = uart_getc(wait);
    if (ret == -1)
        return -1;
    *c = static_cast<char>(ret);
    return 0;
}

void platform_pputc(char c)
{
    uart_pputc(c);
}

int platform_pgetc(char *c, bool wait)
{
     int r = uart_pgetc();
     if (r == -1) {
         return -1;
     }

     *c = static_cast<char>(r);
     return 0;
}

/* stub out the hardware rng entropy generator, which doesn't exist on this platform */
size_t hw_rng_get_entropy(void* buf, size_t len, bool block) {
    return 0;
}

/* no built in framebuffer */
status_t display_get_info(struct display_info *info) {
    return MX_ERR_NOT_FOUND;
}

static void reboot() {
#if BCM2837
#define PM_PASSWORD 0x5a000000
#define PM_RSTC_WRCFG_FULL_RESET 0x00000020
        *REG32(PM_WDOG) =  PM_PASSWORD | 1; // timeout = 1/16th of a second? (whatever)
        *REG32(PM_RSTC) =  PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;
        while(1){
        }
#else
        psci_system_reset();
#ifdef MSM8998_PSHOLD_PHYS
        // Deassert PSHold
        *REG32(paddr_to_kvaddr(MSM8998_PSHOLD_PHYS)) = 0;
#endif
#endif
}


void platform_halt(platform_halt_action suggested_action, platform_halt_reason reason)
{

    if (suggested_action == HALT_ACTION_REBOOT) {
        reboot();
        printf("reboot failed\n");
    } else if (suggested_action == HALT_ACTION_SHUTDOWN) {
        // XXX shutdown seem to not work through psci
        // implement shutdown via pmic
#if BCM2837
        printf("shutdown is unsupported\n");
#else
        psci_system_off();
#endif
    }

#if WITH_LIB_DEBUGLOG
#if WITH_PANIC_BACKTRACE
    thread_print_backtrace(get_current_thread(), __GET_FRAME(0));
#endif
    dlog_bluescreen_halt();
#endif

    if (reason == HALT_REASON_SW_PANIC) {
        if (!halt_on_panic) {
            reboot();
            printf("reboot failed\n");
        }
#if ENABLE_PANIC_SHELL
        dprintf(ALWAYS, "CRASH: starting debug shell... (reason = %d)\n", reason);
        arch_disable_ints();
        panic_shell_start();
#endif  // ENABLE_PANIC_SHELL
    }

    dprintf(ALWAYS, "HALT: spinning forever... (reason = %d)\n", reason);

    // catch all fallthrough cases
    arch_disable_ints();
    for (;;);
}

size_t platform_stow_crashlog(void* log, size_t len) {
    return 0;
}

size_t platform_recover_crashlog(size_t len, void* cookie,
                                 void (*func)(const void* data, size_t, size_t len, void* cookie)) {
    return 0;
}

mx_status_t platform_mexec_patch_bootdata(uint8_t* bootdata, const size_t len) {
    return MX_OK;
}

void platform_mexec(mexec_asm_func mexec_assembly, memmov_ops_t* ops,
                    uintptr_t new_bootimage_addr, size_t new_bootimage_len) {
    mexec_assembly((uintptr_t)new_bootimage_addr, 0, 0, 0, ops,
                   (void*)(MEMBASE + KERNEL_LOAD_OFFSET));
}
