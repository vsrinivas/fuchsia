// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <debug.h>
#include <dev/interrupt.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <magenta/compiler.h>
#include <magenta/process_dispatcher.h>
#include <magenta/types.h>
#include <magenta/vm_object_dispatcher.h>
#include <platform.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

// Warning: The geometry of this struct is depended upon by the mexec assembly
//          function. Do not modify without also updating mexec.S.
typedef struct __PACKED {
    void* dst;
    void* src;
    size_t len;
} memmov_ops_t;

// Implemented in assembly. Copies the new kernel into place and branches to it.
typedef void (*mexec_asm_func)(uint64_t arg0, uint64_t arg1, uint64_t arg2,
                               uint64_t arg3, memmov_ops_t* ops,
                               void* new_kernel_addr);

__BEGIN_CDECLS
extern void mexec_asm(void);
extern void mexec_asm_end(void);
__END_CDECLS

/* Returns the cpuid of the boot cpu.
 *
 * The boot cpu is the cpu responsible for
 * booting the system in start.S, secondary cpus are brought up afterwards.
 * For now we assume that cpuid=0 is the boot cpu but this may change for some
 * SOCs in the future.
 */
static uint get_boot_cpu_id(void) {
    return 0;
}

/* Allocates a page of memory that has the same physical and virtual addressresses.
 */
static mx_status_t identity_page_allocate(void** result_addr) {
    status_t result;

    // Start by obtaining an unused physical page. This address will eventually
    // be the physical/virtual address of our identity mapped page.
    paddr_t pa;
    if (pmm_alloc_page(0, &pa) == nullptr) {
        return ERR_NO_MEMORY;
    }

    // The kernel address space may be in high memory which cannot be identity
    // mapped since all Kernel Virtual Addresses might be out of range of the
    // physical address space. For this reason, we need to make a new address
    // space.
    mxtl::RefPtr<VmAspace> identity_aspace =
            VmAspace::Create(VmAspace::TYPE_LOW_KERNEL, "mexec identity");
    if (!identity_aspace)
        return ERR_INTERNAL;

    // Create a new allocation in the new address space that identity maps the
    // target page.
    const uint perm_flags_rwx = ARCH_MMU_FLAG_PERM_READ  |
                                ARCH_MMU_FLAG_PERM_WRITE |
                                ARCH_MMU_FLAG_PERM_EXECUTE;
    void* identity_address = (void*)pa;
    result = identity_aspace->AllocPhysical("identity mapping", PAGE_SIZE,
                                            &identity_address, 0, pa,
                                            VMM_FLAG_VALLOC_SPECIFIC,
                                            perm_flags_rwx);
    if (result != NO_ERROR)
        return result;

    vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(identity_aspace.get()));

    *result_addr = identity_address;

    return NO_ERROR;
}

/* Migrates the current thread to the CPU identified by target_cpuid. */
static void thread_migrate_cpu(const uint target_cpuid) {
    thread_t *self = get_current_thread();
    const uint old_cpu_id = thread_last_cpu(self);
    LTRACEF("currently on %u, migrating to %u\n", old_cpu_id, target_cpuid);

    thread_set_pinned_cpu(self, target_cpuid);

    mp_reschedule(1 << target_cpuid, 0);

    // When we return from this call, we should have migrated to the target cpu
    thread_yield();

    // Make sure that we have actually migrated.
    const uint current_cpu_id = thread_last_cpu(self);
    DEBUG_ASSERT(current_cpu_id == target_cpuid);

    LTRACEF("previously on %u, migrated to %u\n", old_cpu_id, current_cpu_id);
}

// One of these threads is spun up per CPU and calls halt which does not return.
static int park_cpu_thread(void* arg) {
    // Make sure we're not lopping off the top bits of the arg
    DEBUG_ASSERT(((uintptr_t)arg & 0xffffffff00000000) == 0);
    uint32_t cpu_id = (uint32_t)((uintptr_t)arg & 0xffffffff);

    // From hereon in, this thread will always be assigned to the pinned cpu.
    thread_migrate_cpu(cpu_id);

    LTRACEF("parking cpuid = %u\n", cpu_id);

    arch_disable_ints();

    // This method will not return because the target cpu has halted.
    platform_halt_cpu();

    panic("control should never reach here");
    return -1;
}

/* Takes all the pages in a VMO and creates a copy of them where all the pages
 * occupy a physically contiguous region of physical memory.
 * TODO(gkalsi): Don't coalesce pages into a physically contiguous region and
 *               just pass a vectored I/O list to the mexec assembly.
 */
static mx_status_t vmo_coalesce_pages(mx_handle_t vmo_hdl, paddr_t* addr, size_t* size) {
    DEBUG_ASSERT(addr);
    if (!addr) return ERR_INVALID_ARGS;

    DEBUG_ASSERT(size);
    if (!size) return ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<VmObjectDispatcher> vmo_dispatcher;
    mx_status_t st =
        up->GetDispatcherWithRights(vmo_hdl, MX_RIGHT_READ, &vmo_dispatcher);
    if (st != NO_ERROR)
        return st;

    mxtl::RefPtr<VmObject> vmo = vmo_dispatcher->vmo();

    const size_t num_pages = vmo->AllocatedPages();

    paddr_t base_addr;
    const size_t allocated = pmm_alloc_contiguous(num_pages, PMM_ALLOC_FLAG_ANY,
                                                  0, &base_addr, nullptr);
    if (allocated < num_pages)
        return ERR_NO_MEMORY;

    for (size_t page_offset = 0; page_offset < num_pages; ++page_offset) {
        const off_t byte_offset = page_offset * PAGE_SIZE;

        const paddr_t page_addr = base_addr + byte_offset;

        void* virtual_addr = paddr_to_kvaddr(page_addr);

        size_t bytes_read;
        st = vmo->Read(virtual_addr, byte_offset, PAGE_SIZE, &bytes_read);
        if (st != NO_ERROR || bytes_read != PAGE_SIZE) {
            return ERR_INTERNAL;
        }

        vmo->CleanInvalidateCache(byte_offset, PAGE_SIZE);
        arch_clean_invalidate_cache_range((addr_t)virtual_addr, PAGE_SIZE);
    }

    *size = (num_pages * PAGE_SIZE);
    *addr = base_addr;

    return NO_ERROR;
}

static inline bool intervals_intersect(const void* start1, const size_t len1,
                                       const void* start2, const size_t len2) {
    const void* end1 = (void*)((uintptr_t)start1 + len1);
    const void* end2 = (void*)((uintptr_t)start2 + len2);

    // The start of interval1 is inside interval2
    if (start1 >= start2 && start1 < end2) return true;

    // The end of interval1 is inside interval2
    if (end1 > start2 && end1 <= end2) return true;

    // interval1 completely encloses interval2
    if (start2 >= start1 && end2 <= end1) return true;

    return false;
}

mx_status_t sys_system_mexec(mx_handle_t kernel_vmo,
                             mx_handle_t bootimage_vmo) {
    mx_status_t result;

    // We assume that when the system starts, only one CPU is running. We denote
    // this as the boot CPU.
    // We want to make sure that this is the CPU that eventually branches into
    // the new kernel so we attempt to migrate this thread to that cpu.
    const uint boot_cpu_id = get_boot_cpu_id();
    thread_migrate_cpu(boot_cpu_id);

    paddr_t new_kernel_addr;
    size_t new_kernel_len;
    result = vmo_coalesce_pages(kernel_vmo, &new_kernel_addr, &new_kernel_len);
    if (result != NO_ERROR) {
        printf("Failed to coalesce vmo kernel pages, retcode = %d\n", result);
        return result;
    }

    paddr_t new_bootimage_addr;
    size_t new_bootimage_len;
    result = vmo_coalesce_pages(bootimage_vmo, &new_bootimage_addr, &new_bootimage_len);
    if (result != NO_ERROR) {
        printf("Failed to coalesce vmo bootimage pages ,retcode = %d\n", result);
        return result;
    }

    // WARNING
    // It is unsafe to return from this function beyond this point.
    // This is because we have swapped out the user address space halted the
    // secondary cores and there is no trivial way to bring both of these back.
    void* id_page_addr = 0x0;
    result = identity_page_allocate(&id_page_addr);
    if (result != NO_ERROR) {
        panic("Unable to allocate identity page");
    }

    LTRACEF("mx_system_mexec allocated identity mapped page at %p\n",
            id_page_addr);

    // Create one thread per core to park each core.
    thread_t** park_thread =
        (thread_t**)calloc(arch_max_num_cpus(), sizeof(*park_thread));
    for (uint i = 0; i < arch_max_num_cpus(); i++) {
        // The boot cpu is going to be performing the remainder of the mexec
        // for us so we don't want to park that one.
        if (i == get_boot_cpu_id()) {
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

    // We're going to copy this into our identity page, make sure it's not
    // longer than a single page.
    size_t mexec_asm_length = (uintptr_t)mexec_asm_end - (uintptr_t)mexec_asm;
    DEBUG_ASSERT(mexec_asm_length <= PAGE_SIZE);

    memcpy(id_page_addr, (const void*)mexec_asm, mexec_asm_length);
    arch_sync_cache_range((addr_t)id_page_addr, mexec_asm_length);

    arch_disable_ints();

    // We must pass in an arg that represents a list of memory regions to
    // shuffle around. We put this args list immediately after the mexec
    // assembly.
    uintptr_t ops_ptr = ((((uintptr_t)id_page_addr) + mexec_asm_length + 8) | 0x7) + 1;
    memmov_ops_t* ops = (memmov_ops_t*)(ops_ptr);

    const size_t num_ops = 3;
    // Make sure that we can also pack the arguments in the same page as the
    // final mexec assembly shutdown code.
    DEBUG_ASSERT(((sizeof(*ops) * num_ops + ops_ptr) - (uintptr_t)id_page_addr) < PAGE_SIZE);

    // Op to move the new kernel into place.
    ops[0].src = (void*)new_kernel_addr;
    ops[0].dst = (void*)KERNEL_LOAD_OFFSET;
    ops[0].len = new_kernel_len;

    // Op to move the new bootimage into place (put 64MiB after the Kernel)
    void* dst_addr = (void*)(new_kernel_addr + new_kernel_len + (16 * 1024u * 1024u));
    ops[1].src = (void*)new_bootimage_addr;
    ops[1].dst = dst_addr;
    ops[1].len = new_bootimage_len;

    // Null terminated list.
    ops[2] = { 0, 0, 0 };

    // For now we want to make sure that none of the memcpy intervals overlap.
    // In the future we'll pass vectorized lists to memcpy and copy physical
    // page at a time.
    for (size_t i = 0; i < num_ops; i++) {
        for (size_t j = 0; j < num_ops; j++) {
            if (i == j) continue;
            DEBUG_ASSERT(!intervals_intersect(ops[i].src, ops[i].len,
                                              ops[j].src, ops[j].len));
            DEBUG_ASSERT(!intervals_intersect(ops[i].src, ops[i].len,
                                              ops[j].dst, ops[j].len));
            DEBUG_ASSERT(!intervals_intersect(ops[i].dst, ops[i].len,
                                              ops[j].src, ops[j].len));
            DEBUG_ASSERT(!intervals_intersect(ops[i].dst, ops[i].len,
                                              ops[j].dst, ops[j].len));
        }
    }

    // Sync because there is code in here that we intend to run.
    arch_sync_cache_range((addr_t)id_page_addr, PAGE_SIZE);

    // Clean because we're going to turn the MMU/caches off and we want to make
    // sure that things are still available afterwards.
    arch_clean_cache_range((addr_t)id_page_addr, PAGE_SIZE);

    shutdown_interrupts();

    mp_set_curr_cpu_active(false);
    mp_set_curr_cpu_online(false);

    mexec_asm_func mexec_assembly = (mexec_asm_func)id_page_addr;
    mexec_assembly((uintptr_t)dst_addr, 0, 0, 0, ops, (void*)(MEMBASE + KERNEL_LOAD_OFFSET));

    panic("Execution should never reach here\n");
    return NO_ERROR;
}