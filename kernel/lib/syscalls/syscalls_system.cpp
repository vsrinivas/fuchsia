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
#include <kernel/vm/pmm.h>
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

/* Allocates a page of memory that has the same physical and virtual addressresses.
 */
static mx_status_t identity_page_allocate(void** result_addr) {
    status_t result;

    // Start by obtaining an unused physical page. This address will eventually
    // be the physical/virtual address of our identity mapped page.
    paddr_t pa;
    if (pmm_alloc_page(0, &pa) == nullptr) {
        return MX_ERR_NO_MEMORY;
    }

    // The kernel address space may be in high memory which cannot be identity
    // mapped since all Kernel Virtual Addresses might be out of range of the
    // physical address space. For this reason, we need to make a new address
    // space.
    mxtl::RefPtr<VmAspace> identity_aspace =
            VmAspace::Create(VmAspace::TYPE_LOW_KERNEL, "mexec identity");
    if (!identity_aspace)
        return MX_ERR_INTERNAL;

    // Create a new allocation in the new address space that identity maps the
    // target page.
    const uint perm_flags_rwx = ARCH_MMU_FLAG_PERM_READ  |
                                ARCH_MMU_FLAG_PERM_WRITE |
                                ARCH_MMU_FLAG_PERM_EXECUTE;
    void* identity_address = (void*)pa;
    result = identity_aspace->AllocPhysical("identity mapping", PAGE_SIZE,
                                            &identity_address, 0, pa,
                                            VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
                                            perm_flags_rwx);
    if (result != MX_OK)
        return result;

    vmm_set_active_aspace(reinterpret_cast<vmm_aspace_t*>(identity_aspace.get()));

    *result_addr = identity_address;

    return MX_OK;
}

/* Takes all the pages in a VMO and creates a copy of them where all the pages
 * occupy a physically contiguous region of physical memory.
 * TODO(gkalsi): Don't coalesce pages into a physically contiguous region and
 *               just pass a vectored I/O list to the mexec assembly.
 */
static mx_status_t vmo_coalesce_pages(mx_handle_t vmo_hdl, paddr_t* addr, size_t* size) {
    DEBUG_ASSERT(addr);
    if (!addr) return MX_ERR_INVALID_ARGS;

    DEBUG_ASSERT(size);
    if (!size) return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    mxtl::RefPtr<VmObjectDispatcher> vmo_dispatcher;
    mx_status_t st =
        up->GetDispatcherWithRights(vmo_hdl, MX_RIGHT_READ, &vmo_dispatcher);
    if (st != MX_OK)
        return st;

    mxtl::RefPtr<VmObject> vmo = vmo_dispatcher->vmo();

    const size_t vmo_size = vmo->size();

    const size_t num_pages = ROUNDUP(vmo_size, PAGE_SIZE) / PAGE_SIZE;

    paddr_t base_addr;
    const size_t allocated = pmm_alloc_contiguous(num_pages, PMM_ALLOC_FLAG_ANY,
                                                  0, &base_addr, nullptr);
    if (allocated < num_pages) {
        // TODO(gkalsi): Free pages allocated by pmm_alloc_contiguous pages
        //               and return an error.
        panic("Failed to allocate contiguous memory");
    }

    uint8_t* dst_addr = (uint8_t*)paddr_to_kvaddr(base_addr);

    size_t bytes_remaining = vmo_size;
    size_t offset = 0;
    while (bytes_remaining) {
        size_t bytes_read;

        st = vmo->Read(dst_addr + offset, offset, bytes_remaining, &bytes_read);

        if (st != MX_OK || bytes_read == 0) {
            // TODO(gkalsi): Free pages allocated by pmm_alloc_contiguous pages
            //               and return an error.
            panic("Failed to read to contiguous vmo");
        }

        bytes_remaining -= bytes_read;
        offset += bytes_read;
    }

    arch_clean_invalidate_cache_range((addr_t)dst_addr, vmo_size);

    *size = vmo_size;
    *addr = base_addr;

    return MX_OK;
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

    paddr_t new_kernel_addr;
    size_t new_kernel_len;
    result = vmo_coalesce_pages(kernel_vmo, &new_kernel_addr, &new_kernel_len);
    if (result != MX_OK) {
        printf("Failed to coalesce vmo kernel pages, retcode = %d\n", result);
        return result;
    }

    paddr_t new_bootimage_addr;
    size_t new_bootimage_len;
    result = vmo_coalesce_pages(bootimage_vmo, &new_bootimage_addr, &new_bootimage_len);
    if (result != MX_OK) {
        printf("Failed to coalesce vmo bootimage pages ,retcode = %d\n", result);
        return result;
    }

    // WARNING
    // It is unsafe to return from this function beyond this point.
    // This is because we have swapped out the user address space halted the
    // secondary cores and there is no trivial way to bring both of these back.
    void* id_page_addr = 0x0;
    result = identity_page_allocate(&id_page_addr);
    if (result != MX_OK) {
        panic("Unable to allocate identity page");
    }

    LTRACEF("mx_system_mexec allocated identity mapped page at %p\n",
            id_page_addr);

    // We assume that when the system starts, only one CPU is running. We denote
    // this as the boot CPU.
    // We want to make sure that this is the CPU that eventually branches into
    // the new kernel so we attempt to migrate this thread to that cpu.
    thread_migrate_cpu(BOOT_CPU_ID);
    platform_halt_secondary_cpus();

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

    const size_t num_ops = 2;
    // Make sure that we can also pack the arguments in the same page as the
    // final mexec assembly shutdown code.
    DEBUG_ASSERT(((sizeof(*ops) * num_ops + ops_ptr) - (uintptr_t)id_page_addr) < PAGE_SIZE);

    // Op to move the new kernel into place.
    ops[0].src = (void*)new_kernel_addr;
    ops[0].dst = (void*)(MEMBASE + KERNEL_LOAD_OFFSET);
    ops[0].len = new_kernel_len;

    // Null terminated list.
    ops[1] = { 0, 0, 0 };

    // Make sure that the kernel, when copied, will not overwrite the bootdata.
    DEBUG_ASSERT(!intervals_intersect(ops[0].dst, ops[0].len,
                                      (const void*)new_bootimage_addr,
                                      new_bootimage_len));

    // Sync because there is code in here that we intend to run.
    arch_sync_cache_range((addr_t)id_page_addr, PAGE_SIZE);

    // Clean because we're going to turn the MMU/caches off and we want to make
    // sure that things are still available afterwards.
    arch_clean_cache_range((addr_t)id_page_addr, PAGE_SIZE);

    shutdown_interrupts();

    mp_set_curr_cpu_active(false);
    mp_set_curr_cpu_online(false);

    mexec_asm_func mexec_assembly = (mexec_asm_func)id_page_addr;
    mexec_assembly((uintptr_t)new_bootimage_addr, 0, 0, 0, ops,
                   (void*)(MEMBASE + KERNEL_LOAD_OFFSET));

    panic("Execution should never reach here\n");
    return MX_OK;
}
