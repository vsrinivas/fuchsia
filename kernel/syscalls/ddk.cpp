// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>

#include <dev/interrupt.h>
#include <dev/iommu.h>
#include <dev/udisplay.h>
#include <vm/vm.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>
#include <lib/user_copy/user_ptr.h>
#include <object/bus_transaction_initiator_dispatcher.h>
#include <object/handle.h>
#include <object/interrupt_dispatcher.h>
#include <object/interrupt_event_dispatcher.h>
#include <object/virtual_interrupt_dispatcher.h>
#include <object/iommu_dispatcher.h>
#include <object/process_dispatcher.h>
#include <object/resources.h>
#include <object/vm_object_dispatcher.h>
#include <zxcpp/new.h>


#if ARCH_X86
#include <platform/pc/bootloader.h>
#endif

#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/pci.h>
#include <zircon/syscalls/smc.h>
#include <fbl/auto_call.h>
#include <fbl/inline_array.h>

#include "ddk_priv.h"
#include "priv.h"

#define LOCAL_TRACE 0

zx_status_t sys_vmo_create_contiguous(zx_handle_t bti, size_t size, uint32_t alignment_log2,
                                      user_out_handle* out) {
    LTRACEF("size 0x%zu\n", size);

    if (size == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (alignment_log2 == 0) {
        alignment_log2 = PAGE_SIZE_SHIFT;
    }
    // catch obviously wrong values
    if (alignment_log2 < PAGE_SIZE_SHIFT || alignment_log2 >= (8 * sizeof(uint64_t))) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<BusTransactionInitiatorDispatcher> bti_dispatcher;
    zx_status_t status = up->GetDispatcherWithRights(bti, ZX_RIGHT_MAP, &bti_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    auto align_log2_arg = static_cast<uint8_t>(alignment_log2);

    // create a vm object
    fbl::RefPtr<VmObject> vmo;
    status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, size, align_log2_arg, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    // create a Vm Object dispatcher
    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    zx_status_t result = VmObjectDispatcher::Create(fbl::move(vmo), &dispatcher, &rights);
    if (result != ZX_OK) {
        return result;
    }

    // create a handle and attach the dispatcher to it
    return out->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_vmo_create_physical(zx_handle_t hrsrc, uintptr_t paddr, size_t size,
                                    user_out_handle* out) {
    LTRACEF("size 0x%zu\n", size);

    // TODO: attempting to create a physical VMO that points to memory should be an error

    zx_status_t status;
    if ((status = validate_resource_mmio(hrsrc, paddr, size)) < 0) {
        return status;
    }

    size = ROUNDUP_PAGE_SIZE(size);

    // create a vm object
    fbl::RefPtr<VmObject> vmo;
    zx_status_t result = VmObjectPhysical::Create(paddr, size, &vmo);
    if (result != ZX_OK) {
        return result;
    }

    // create a Vm Object dispatcher
    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    result = VmObjectDispatcher::Create(fbl::move(vmo), &dispatcher, &rights);
    if (result != ZX_OK)
        return result;

    // create a handle and attach the dispatcher to it
    return out->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_framebuffer_get_info(zx_handle_t handle, user_out_ptr<uint32_t> format,
                                     user_out_ptr<uint32_t> width, user_out_ptr<uint32_t> height,
                                     user_out_ptr<uint32_t> stride) {
    zx_status_t status;
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0)
        return status;
#if ARCH_X86
    if (!bootloader.fb.base)
        return ZX_ERR_INVALID_ARGS;
    status = format.copy_to_user(bootloader.fb.format);
    if (status != ZX_OK)
        return status;
    status = width.copy_to_user(bootloader.fb.width);
    if (status != ZX_OK)
        return status;
    status = height.copy_to_user(bootloader.fb.height);
    if (status != ZX_OK)
        return status;
    status = stride.copy_to_user(bootloader.fb.stride);
    if (status != ZX_OK)
        return status;
    return ZX_OK;
#else
    return ZX_ERR_NOT_SUPPORTED;
#endif
}

zx_status_t sys_framebuffer_set_range(zx_handle_t hrsrc, zx_handle_t vmo_handle, uint32_t len, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    zx_status_t status;
    if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0)
        return status;

    if (vmo_handle == ZX_HANDLE_INVALID) {
        udisplay_clear_framebuffer_vmo();
        return ZX_OK;
    }

    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmObjectDispatcher> vmo;
    status = up->GetDispatcher(vmo_handle, &vmo);
    if (status != ZX_OK)
        return status;

    status = udisplay_set_framebuffer(vmo->vmo());
    if (status != ZX_OK)
        return status;

    struct display_info di;
    memset(&di, 0, sizeof(struct display_info));
    di.format = format;
    di.width = width;
    di.height = height;
    di.stride = stride;
    di.flags = DISPLAY_FLAG_HW_FRAMEBUFFER;
    udisplay_set_display_info(&di);

    return ZX_OK;
}

zx_status_t sys_iommu_create(zx_handle_t rsrc_handle, uint32_t type,
                             user_in_ptr<const void> desc, size_t desc_len,
                             user_out_handle* out) {
    // TODO: finer grained validation
    zx_status_t status;
    if ((status = validate_resource(rsrc_handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    if (desc_len > ZX_IOMMU_MAX_DESC_LEN) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;

    {
        // Copy the descriptor into the kernel and try to create the dispatcher
        // using it.
        fbl::AllocChecker ac;
        fbl::unique_ptr<uint8_t[]> copied_desc(new (&ac) uint8_t[desc_len]);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        if ((status = desc.copy_array_from_user(copied_desc.get(), desc_len)) != ZX_OK) {
            return status;
        }
        status = IommuDispatcher::Create(type,
                                         fbl::unique_ptr<const uint8_t[]>(copied_desc.release()),
                                         desc_len, &dispatcher, &rights);
        if (status != ZX_OK) {
            return status;
        }
    }

    return out->make(fbl::move(dispatcher), rights);
}

#if ARCH_X86
#include <arch/x86/descriptor.h>
#include <arch/x86/ioport.h>

zx_status_t sys_ioports_request(zx_handle_t hrsrc, uint16_t io_addr, uint32_t len) {
    zx_status_t status;
    if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }

    LTRACEF("addr 0x%x len 0x%x\n", io_addr, len);

    return IoBitmap::GetCurrent().SetIoBitmap(io_addr, len, 1);
}
#else
zx_status_t sys_ioports_request(zx_handle_t hrsrc, uint16_t io_addr, uint32_t len) {
    // doesn't make sense on non-x86
    return ZX_ERR_NOT_SUPPORTED;
}
#endif

zx_status_t sys_pc_firmware_tables(zx_handle_t hrsrc, user_out_ptr<zx_paddr_t> acpi_rsdp,
                                   user_out_ptr<zx_paddr_t> smbios) {
    // TODO(ZX-971): finer grained validation
    zx_status_t status;
    if ((status = validate_resource(hrsrc, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }
#if ARCH_X86
    if ((status = acpi_rsdp.copy_to_user(bootloader.acpi_rsdp)) != ZX_OK) {
        return status;
    }
    if ((status = smbios.copy_to_user(bootloader.smbios)) != ZX_OK) {
        return status;
    }

    return ZX_OK;
#endif
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t sys_bti_create(zx_handle_t iommu, uint32_t options, uint64_t bti_id,
                           user_out_handle* out) {
    auto up = ProcessDispatcher::GetCurrent();

    if (options != 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<IommuDispatcher> iommu_dispatcher;
    // TODO(teisenbe): This should probably have a right on it.
    zx_status_t status = up->GetDispatcherWithRights(iommu, ZX_RIGHT_NONE, &iommu_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    // TODO(teisenbe): Migrate BusTransactionInitiatorDispatcher::Create to
    // taking the iommu_dispatcher
    status = BusTransactionInitiatorDispatcher::Create(iommu_dispatcher->iommu(), bti_id,
                                                       &dispatcher, &rights);
    if (status != ZX_OK) {
        return status;
    }

    return out->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_bti_pin(zx_handle_t bti, uint32_t options, zx_handle_t vmo, uint64_t offset,
                        uint64_t size, user_out_ptr<zx_paddr_t> addrs, size_t addrs_count,
                        user_out_handle* pmt) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<BusTransactionInitiatorDispatcher> bti_dispatcher;
    zx_status_t status = up->GetDispatcherWithRights(bti, ZX_RIGHT_MAP, &bti_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    if (!IS_PAGE_ALIGNED(offset) || !IS_PAGE_ALIGNED(size)) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<VmObjectDispatcher> vmo_dispatcher;
    zx_rights_t vmo_rights;
    status = up->GetDispatcherAndRights(vmo, &vmo_dispatcher, &vmo_rights);
    if (status != ZX_OK) {
        return status;
    }
    if (!(vmo_rights & ZX_RIGHT_MAP)) {
        return ZX_ERR_ACCESS_DENIED;
    }

    // Convert requested permissions and check against VMO rights
    uint32_t iommu_perms = 0;
    bool compress_results = false;
    if (options & ZX_BTI_PERM_READ) {
        if (!(vmo_rights & ZX_RIGHT_READ)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        iommu_perms |= IOMMU_FLAG_PERM_READ;
        options &= ~ZX_BTI_PERM_READ;
    }
    if (options & ZX_BTI_PERM_WRITE) {
        if (!(vmo_rights & ZX_RIGHT_WRITE)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        iommu_perms |= IOMMU_FLAG_PERM_WRITE;
        options &= ~ZX_BTI_PERM_WRITE;
    }
    if (options & ZX_BTI_PERM_EXECUTE) {
        if (!(vmo_rights & ZX_RIGHT_EXECUTE)) {
            return ZX_ERR_ACCESS_DENIED;
        }
        iommu_perms |= IOMMU_FLAG_PERM_EXECUTE;
        options &= ~ZX_BTI_PERM_EXECUTE;
    }
    if (options & ZX_BTI_COMPRESS) {
        compress_results = true;
        options &= ~ZX_BTI_COMPRESS;
    }
    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }

    constexpr size_t kAddrsLenLimitForStack = 4;
    fbl::AllocChecker ac;
    fbl::InlineArray<dev_vaddr_t, kAddrsLenLimitForStack> mapped_addrs(&ac, addrs_count);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    fbl::RefPtr<Dispatcher> new_pmt;
    zx_rights_t new_pmt_rights;
    status = bti_dispatcher->Pin(vmo_dispatcher->vmo(), offset, size, iommu_perms, &new_pmt,
                                 &new_pmt_rights);
    if (status != ZX_OK) {
        return status;
    }

    status = static_cast<PinnedMemoryTokenDispatcher*>(new_pmt.get())
            ->EncodeAddrs(compress_results, mapped_addrs.get(), addrs_count);
    if (status != ZX_OK) {
        return status;
    }

    static_assert(sizeof(dev_vaddr_t) == sizeof(zx_paddr_t), "mismatched types");
    if ((status = addrs.copy_array_to_user(mapped_addrs.get(), addrs_count)) != ZX_OK) {
        return status;
    }

    return pmt->make(fbl::move(new_pmt), new_pmt_rights);
}

zx_status_t sys_bti_release_quarantine(zx_handle_t bti) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<BusTransactionInitiatorDispatcher> bti_dispatcher;

    zx_status_t status = up->GetDispatcherWithRights(bti, ZX_RIGHT_WRITE, &bti_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    bti_dispatcher->ReleaseQuarantine();
    return ZX_OK;
}

// Having a single-purpose syscall like this is a bit of an anti-pattern in our
// syscall API, but we feel there is benefit in this over trying to extend the
// semantics of handle closing in sys_handle_close and process death.  In
// particular, PMTs are the only objects in the system that track the lifetime
// of something external to the process model (external hardware DMA
// capabilities).
zx_status_t sys_pmt_unpin(zx_handle_t pmt) {
    auto up = ProcessDispatcher::GetCurrent();

    HandleOwner handle = up->RemoveHandle(pmt);
    if (!handle)
        return ZX_ERR_BAD_HANDLE;
    fbl::RefPtr<Dispatcher> dispatcher = handle->dispatcher();
    auto pmt_dispatcher = DownCastDispatcher<PinnedMemoryTokenDispatcher>(&dispatcher);
    if (!pmt_dispatcher)
        return ZX_ERR_WRONG_TYPE;

    pmt_dispatcher->MarkUnpinned();

    return ZX_OK;
}

zx_status_t sys_interrupt_create(zx_handle_t src_obj, uint32_t src_num,
                           uint32_t options, user_out_handle* out_handle) {
    LTRACEF("options 0x%x\n", options);

    // resource not required for virtual interrupts
    if (!(options & ZX_INTERRUPT_VIRTUAL)) {
        // TODO(ZX-971): finer grained validation
        zx_status_t status;
        if ((status = validate_resource(src_obj, ZX_RSRC_KIND_ROOT)) < 0) {
            return status;
        }
    }

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    zx_status_t result;
    if (options & ZX_INTERRUPT_VIRTUAL) {
        result = VirtualInterruptDispatcher::Create(&dispatcher, &rights, options);
    } else {
        result = InterruptEventDispatcher::Create(&dispatcher, &rights, src_num, options);
    }
    if (result != ZX_OK)
        return result;

    return out_handle->make(fbl::move(dispatcher), rights);
}

zx_status_t sys_interrupt_bind(zx_handle_t inth, zx_handle_t porth,
                         uint64_t key, uint32_t options) {
    LTRACEF("handle %x\n", inth);
    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status;
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<InterruptDispatcher> interrupt;
    status = up->GetDispatcherWithRights(inth, ZX_RIGHT_READ, &interrupt);
    if (status != ZX_OK)
        return status;

    fbl::RefPtr<PortDispatcher> port;
    status = up->GetDispatcherWithRights(porth, ZX_RIGHT_WRITE, &port);
    if (status != ZX_OK)
        return status;

    if (!port->can_bind_to_interrupt()) {
        return ZX_ERR_WRONG_TYPE;
    }

    return interrupt->Bind(port, interrupt, key);
}

zx_status_t sys_interrupt_ack(zx_handle_t inth) {
    LTRACEF("handle %x\n", inth);

    zx_status_t status;
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<InterruptDispatcher> interrupt;
    status = up->GetDispatcherWithRights(inth, ZX_RIGHT_WRITE, &interrupt);
    if (status != ZX_OK)
        return status;
    return interrupt->Ack();
}

zx_status_t sys_interrupt_wait(zx_handle_t handle, user_out_ptr<zx_time_t> out_timestamp) {
    LTRACEF("handle %x\n", handle);

    zx_status_t status;
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<InterruptDispatcher> interrupt;
    status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WAIT, &interrupt);
    if (status != ZX_OK)
        return status;

    zx_time_t timestamp;
    status = interrupt->WaitForInterrupt(&timestamp);
    if (status == ZX_OK && out_timestamp)
        status = out_timestamp.copy_to_user(timestamp);
    return status;
}

zx_status_t sys_interrupt_destroy(zx_handle_t handle) {
    LTRACEF("handle %x\n", handle);

    zx_status_t status;
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<InterruptDispatcher> interrupt;
    status = up->GetDispatcher(handle, &interrupt);
    if (status != ZX_OK)
        return status;
    return interrupt->Destroy();
}

zx_status_t sys_interrupt_trigger(zx_handle_t handle,
                            uint32_t options,
                            zx_time_t timestamp) {
    LTRACEF("handle %x\n", handle);

    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status;
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<InterruptDispatcher> interrupt;
    status = up->GetDispatcherWithRights(handle, ZX_RIGHT_SIGNAL, &interrupt);
    if (status != ZX_OK)
        return status;

    return interrupt->Trigger(timestamp);
}

zx_status_t sys_smc_call(zx_handle_t handle,
                         user_in_ptr<const zx_smc_parameters_t> parameters,
                         user_out_ptr<zx_smc_result_t> out_smc_result) {
    zx_status_t status;
    // TODO(ZX-971): finer grained validation
    if ((status = validate_resource(handle, ZX_RSRC_KIND_ROOT)) < 0) {
        return status;
    }
    if (!parameters || !out_smc_result) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_smc_parameters_t params;
    zx_smc_result_t result;

    status = parameters.copy_from_user(&params);
    if (status != ZX_OK) {
        return status;
    }

    arch_smc_call(&params, &result);

    return out_smc_result.copy_to_user(result);
}
