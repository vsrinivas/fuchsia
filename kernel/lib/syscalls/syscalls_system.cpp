// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arch_ops.h>
#include <arch/mp.h>
#include <debug.h>
#include <dev/interrupt.h>
#include <kernel/cmdline.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>
#include <magenta/boot/bootdata.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <mexec.h>
#include <object/process_dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <platform.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

// Allocate this many extra bytes at the end of the bootdata for the platform
// to fill in with platform specific boot structures.
const size_t kBootdataPlatformExtraBytes = PAGE_SIZE;

__BEGIN_CDECLS
extern void mexec_asm(void);
extern void mexec_asm_end(void);
__END_CDECLS

/* Allocates a page of memory that has the same physical and virtual addressresses.
 */
static mx_status_t identity_page_allocate(void** result_addr) {
    mx_status_t result;

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
    fbl::RefPtr<VmAspace> identity_aspace =
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
static mx_status_t vmo_coalesce_pages(mx_handle_t vmo_hdl, const size_t extra_bytes,
                                      paddr_t* addr, uint8_t** vaddr, size_t* size) {
    DEBUG_ASSERT(addr);
    if (!addr) return MX_ERR_INVALID_ARGS;

    DEBUG_ASSERT(size);
    if (!size) return MX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<VmObjectDispatcher> vmo_dispatcher;
    mx_status_t st =
        up->GetDispatcherWithRights(vmo_hdl, MX_RIGHT_READ, &vmo_dispatcher);
    if (st != MX_OK)
        return st;

    fbl::RefPtr<VmObject> vmo = vmo_dispatcher->vmo();

    const size_t vmo_size = vmo->size();

    const size_t num_pages = ROUNDUP(vmo_size + extra_bytes, PAGE_SIZE) / PAGE_SIZE;

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

    *size = num_pages * PAGE_SIZE;
    *addr = base_addr;
    if (vaddr)
        *vaddr = dst_addr;

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


/* Takes a buffer to a bootimage and appends a section to the end of it */
mx_status_t bootdata_append_section(uint8_t* bootdata_buf, const size_t buflen,
                                    const uint8_t* section, const uint32_t section_length,
                                    const uint32_t type, const uint32_t extra,
                                    const uint32_t flags) {
    bootdata_t* hdr = (bootdata_t*)bootdata_buf;

    if ((hdr->type != BOOTDATA_CONTAINER) ||
        (hdr->extra != BOOTDATA_MAGIC) ||
        (hdr->flags != 0)) {
        // This buffer does not point to a bootimage.
        return MX_ERR_WRONG_TYPE;
    }

    size_t total_len = hdr->length + sizeof(*hdr);
    size_t new_section_length = BOOTDATA_ALIGN(section_length) + sizeof(bootdata_t);

    // Make sure there's enough buffer space after the bootdata container to
    // append the new section.
    if ((total_len + new_section_length) > buflen) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }

    // Seek to the end of the bootimage.
    bootdata_buf += total_len;

    bootdata_t* new_hdr = (bootdata_t*)bootdata_buf;
    new_hdr->type = type;
    new_hdr->length = section_length;
    new_hdr->extra = extra;
    new_hdr->flags = flags;

    bootdata_buf += sizeof(*new_hdr);

    memcpy(bootdata_buf, section, section_length);

    hdr->length += (uint32_t)new_section_length;

    return MX_OK;
}

static mx_status_t bootdata_append_cmdline(user_ptr<const char> _cmdlinebuf,
                                           const size_t cmdlinebuf_size,
                                           uint8_t* bootimage_buffer,
                                           const size_t bootimage_buflen) {
    // Bootdata section header length fields are uint32_ts. Make sure we're not
    // truncating the command line length by casting it from size_t to a uint32_t.
    static_assert(CMDLINE_MAX <= UINT32_MAX, "cmdline max must fit into bootdata header");

    // If the user passes us a command line that's longer than our limit, we
    // fail immediately.
    if (cmdlinebuf_size > CMDLINE_MAX) {
        return MX_ERR_INVALID_ARGS;
    }

    // No command line provided, nothing to be done.
    if (cmdlinebuf_size == 0) {
        return MX_OK;
    }

    // Allocate a buffer large enough to accomodate the whole command line.
    // Use brace initializers to initialize this buffer to 0.
    fbl::AllocChecker ac;
    auto deleter = fbl::unique_ptr<char[]>(new (&ac) char[CMDLINE_MAX]{});
    char* buffer = deleter.get();
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    // Copy contents from the user buffer to the local kernel buffer.
    _cmdlinebuf.copy_array_from_user(buffer, cmdlinebuf_size);

    // Ensure that the user provided string buffer is either already null
    // terminated or that it can be null terminated without blowing the buffer.
    // Add 1 to the result of strnlen to ensure that we account for the null
    // terminator.
    const size_t cmdline_len = strnlen(buffer, cmdlinebuf_size) + 1;
    if (cmdline_len > CMDLINE_MAX) {
        return MX_ERR_INVALID_ARGS;
    }

    return bootdata_append_section(bootimage_buffer, bootimage_buflen,
                                   reinterpret_cast<const uint8_t*>(buffer),
                                   static_cast<uint32_t>(cmdline_len),
                                   BOOTDATA_CMDLINE, 0, 0);
}

mx_status_t sys_system_mexec(mx_handle_t kernel_vmo, mx_handle_t bootimage_vmo,
                             user_ptr<const char> _cmdline, uint32_t cmdline_len) {
    mx_status_t result;

    paddr_t new_kernel_addr;
    size_t new_kernel_len;
    result = vmo_coalesce_pages(kernel_vmo, 0, &new_kernel_addr, nullptr,
                                &new_kernel_len);
    if (result != MX_OK) {
        return result;
    }

    paddr_t new_bootimage_addr;
    uint8_t* bootimage_buffer;
    size_t new_bootimage_len;
    result = vmo_coalesce_pages(bootimage_vmo, kBootdataPlatformExtraBytes + cmdline_len,
                                &new_bootimage_addr, &bootimage_buffer,
                                &new_bootimage_len);
    if (result != MX_OK) {
        return result;
    }

    result = bootdata_append_cmdline(_cmdline, cmdline_len, bootimage_buffer, new_bootimage_len);
    if (result != MX_OK) {
        return result;
    }


    // WARNING
    // It is unsafe to return from this function beyond this point.
    // This is because we have swapped out the user address space and halted the
    // secondary cores and there is no trivial way to bring both of these back.
    thread_migrate_cpu(BOOT_CPU_ID);

    // Allow the platform to patch the bootdata with any platform specific
    // sections before mexecing.
    result = platform_mexec_patch_bootdata(bootimage_buffer, new_bootimage_len);
    if (result != MX_OK) {
        panic("Error while patching platform bootdata.");
    }

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

    // Ask the platform to mexec into the next kernel.
    mexec_asm_func mexec_assembly = (mexec_asm_func)id_page_addr;
    platform_mexec(mexec_assembly, ops, new_bootimage_addr, new_bootimage_len);

    panic("Execution should never reach here\n");
    return MX_OK;
}
