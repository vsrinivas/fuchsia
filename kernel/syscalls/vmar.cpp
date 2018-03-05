// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <vm/vm_object.h>
#include <vm/vm_address_region.h>

#include <lib/user_copy/user_ptr.h>

#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>

#include "priv.h"

#define LOCAL_TRACE 0

zx_status_t sys_vmar_allocate(zx_handle_t parent_vmar_handle,
                              size_t offset, size_t size, uint32_t map_flags,
                              user_out_handle* child_vmar,
                              user_out_ptr<uintptr_t> child_addr) {

    auto up = ProcessDispatcher::GetCurrent();

    // Compute needed rights from requested mapping protections.
    zx_rights_t vmar_rights = 0u;
    if (map_flags & ZX_VM_FLAG_CAN_MAP_READ)
        vmar_rights |= ZX_RIGHT_READ;
    if (map_flags & ZX_VM_FLAG_CAN_MAP_WRITE)
        vmar_rights |= ZX_RIGHT_WRITE;
    if (map_flags & ZX_VM_FLAG_CAN_MAP_EXECUTE)
        vmar_rights |= ZX_RIGHT_EXECUTE;

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_status_t status = up->GetDispatcherWithRights(parent_vmar_handle, vmar_rights, &vmar);
    if (status != ZX_OK)
        return status;

    // Create the new VMAR
    fbl::RefPtr<VmAddressRegionDispatcher> new_vmar;
    zx_rights_t new_rights;
    status = vmar->Allocate(offset, size, map_flags, &new_vmar, &new_rights);
    if (status != ZX_OK)
        return status;

    // Setup a handler to destroy the new VMAR if the syscall is unsuccessful.
    // Note that new_vmar is being passed by value, so a new reference is held
    // there.
    auto cleanup_handler = fbl::MakeAutoCall([new_vmar]() {
        new_vmar->Destroy();
    });

    // Extract the base address before we give away the ref.
    uintptr_t base = new_vmar->vmar()->base();

    // Create a handle and attach the dispatcher to it
    status = child_vmar->make(fbl::move(new_vmar), new_rights);

    if (status == ZX_OK)
        status = child_addr.copy_to_user(base);

    if (status == ZX_OK)
        cleanup_handler.cancel();
    return status;
}

zx_status_t sys_vmar_destroy(zx_handle_t vmar_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_status_t status = up->GetDispatcher(vmar_handle, &vmar);
    if (status != ZX_OK)
        return status;

    return vmar->Destroy();
}

zx_status_t sys_vmar_map(zx_handle_t vmar_handle, size_t vmar_offset,
                         zx_handle_t vmo_handle, uint64_t vmo_offset, size_t len, uint32_t map_flags,
                         user_out_ptr<uintptr_t> mapped_addr) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the VMAR dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_rights_t vmar_rights;
    zx_status_t status = up->GetDispatcherAndRights(vmar_handle, &vmar, &vmar_rights);
    if (status != ZX_OK)
        return status;

    // lookup the VMO dispatcher from handle
    fbl::RefPtr<VmObjectDispatcher> vmo;
    zx_rights_t vmo_rights;
    status = up->GetDispatcherAndRights(vmo_handle, &vmo, &vmo_rights);
    if (status != ZX_OK)
        return status;

    // test to see if we should even be able to map this
    if (!(vmo_rights & ZX_RIGHT_MAP))
        return ZX_ERR_ACCESS_DENIED;

    if (!VmAddressRegionDispatcher::is_valid_mapping_protection(map_flags))
        return ZX_ERR_INVALID_ARGS;

    bool do_map_range = false;
    if (map_flags & ZX_VM_FLAG_MAP_RANGE) {
        do_map_range = true;
        map_flags &= ~ZX_VM_FLAG_MAP_RANGE;
    }

    if (do_map_range && (map_flags & ZX_VM_FLAG_SPECIFIC_OVERWRITE)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Usermode is not allowed to specify these flags on mappings, though we may
    // set them below.
    if (map_flags & (ZX_VM_FLAG_CAN_MAP_READ | ZX_VM_FLAG_CAN_MAP_WRITE | ZX_VM_FLAG_CAN_MAP_EXECUTE)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Permissions allowed by both the VMO and the VMAR
    const bool can_read = (vmo_rights & ZX_RIGHT_READ) && (vmar_rights & ZX_RIGHT_READ);
    const bool can_write = (vmo_rights & ZX_RIGHT_WRITE) && (vmar_rights & ZX_RIGHT_WRITE);
    const bool can_exec = (vmo_rights & ZX_RIGHT_EXECUTE) && (vmar_rights & ZX_RIGHT_EXECUTE);

    // test to see if the requested mapping protections are allowed
    if ((map_flags & ZX_VM_FLAG_PERM_READ) && !can_read)
        return ZX_ERR_ACCESS_DENIED;
    if ((map_flags & ZX_VM_FLAG_PERM_WRITE) && !can_write)
        return ZX_ERR_ACCESS_DENIED;
    if ((map_flags & ZX_VM_FLAG_PERM_EXECUTE) && !can_exec)
        return ZX_ERR_ACCESS_DENIED;

    // If a permission is allowed by both the VMO and the VMAR, add it to the
    // flags for the new mapping, so that the VMO's rights as of now can be used
    // to constrain future permission changes via Protect().
    if (can_read)
        map_flags |= ZX_VM_FLAG_CAN_MAP_READ;
    if (can_write)
        map_flags |= ZX_VM_FLAG_CAN_MAP_WRITE;
    if (can_exec)
        map_flags |= ZX_VM_FLAG_CAN_MAP_EXECUTE;

    fbl::RefPtr<VmMapping> vm_mapping;
    status = vmar->Map(vmar_offset, vmo->vmo(), vmo_offset, len, map_flags, &vm_mapping);
    if (status != ZX_OK)
        return status;

    // Setup a handler to destroy the new mapping if the syscall is unsuccessful.
    auto cleanup_handler = fbl::MakeAutoCall([vm_mapping]() {
        vm_mapping->Destroy();
    });

    if (do_map_range) {
        status = vm_mapping->MapRange(vmo_offset, len, false);
        if (status != ZX_OK) {
            return status;
        }
    }

    status = mapped_addr.copy_to_user(vm_mapping->base());
    if (status != ZX_OK)
        return status;

    cleanup_handler.cancel();
    return ZX_OK;
}

zx_status_t sys_vmar_unmap(zx_handle_t vmar_handle, uintptr_t addr, size_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_status_t status = up->GetDispatcher(vmar_handle, &vmar);
    if (status != ZX_OK)
        return status;

    return vmar->Unmap(addr, len);
}

zx_status_t sys_vmar_protect(zx_handle_t vmar_handle, uintptr_t addr, size_t len, uint32_t prot) {
    auto up = ProcessDispatcher::GetCurrent();

    zx_rights_t vmar_rights = 0u;
    if (prot & ZX_VM_FLAG_PERM_READ)
        vmar_rights |= ZX_RIGHT_READ;
    if (prot & ZX_VM_FLAG_PERM_WRITE)
        vmar_rights |= ZX_RIGHT_WRITE;
    if (prot & ZX_VM_FLAG_PERM_EXECUTE)
        vmar_rights |= ZX_RIGHT_EXECUTE;

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    zx_status_t status = up->GetDispatcherWithRights(vmar_handle, vmar_rights, &vmar);
    if (status != ZX_OK)
        return status;

    if (!VmAddressRegionDispatcher::is_valid_mapping_protection(prot))
        return ZX_ERR_INVALID_ARGS;

    return vmar->Protect(addr, len, prot);
}
