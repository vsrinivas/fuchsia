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

#include <object/handle_owner.h>
#include <object/handles.h>
#include <object/process_dispatcher.h>
#include <object/vm_address_region_dispatcher.h>
#include <object/vm_object_dispatcher.h>

#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>

#include "syscalls_priv.h"

#define LOCAL_TRACE 0

mx_status_t sys_vmar_allocate(mx_handle_t parent_vmar_handle,
                    size_t offset, size_t size, uint32_t map_flags,
                    user_ptr<mx_handle_t> _child_vmar, user_ptr<uintptr_t> _child_addr) {

    auto up = ProcessDispatcher::GetCurrent();

    // Compute needed rights from requested mapping protections.
    mx_rights_t vmar_rights = 0u;
    if (map_flags & MX_VM_FLAG_CAN_MAP_READ)
        vmar_rights |= MX_RIGHT_READ;
    if (map_flags & MX_VM_FLAG_CAN_MAP_WRITE)
        vmar_rights |= MX_RIGHT_WRITE;
    if (map_flags & MX_VM_FLAG_CAN_MAP_EXECUTE)
        vmar_rights |= MX_RIGHT_EXECUTE;

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_status_t status = up->GetDispatcherWithRights(parent_vmar_handle, vmar_rights, &vmar);
    if (status != MX_OK)
        return status;

    // Create the new VMAR
    fbl::RefPtr<VmAddressRegionDispatcher> new_vmar;
    mx_rights_t new_rights;
    status = vmar->Allocate(offset, size, map_flags, &new_vmar, &new_rights);
    if (status != MX_OK)
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
    HandleOwner handle(MakeHandle(fbl::move(new_vmar), new_rights));
    if (!handle)
        return MX_ERR_NO_MEMORY;

    if (_child_addr.copy_to_user(base) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    if (_child_vmar.copy_to_user(up->MapHandleToValue(handle)) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    up->AddHandle(fbl::move(handle));
    cleanup_handler.cancel();
    return MX_OK;
}

mx_status_t sys_vmar_destroy(mx_handle_t vmar_handle) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_status_t status = up->GetDispatcher(vmar_handle, &vmar);
    if (status != MX_OK)
        return status;

    return vmar->Destroy();
}

mx_status_t sys_vmar_map(mx_handle_t vmar_handle, size_t vmar_offset,
                    mx_handle_t vmo_handle, uint64_t vmo_offset, size_t len, uint32_t map_flags,
                    user_ptr<uintptr_t> _mapped_addr) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the VMAR dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_rights_t vmar_rights;
    mx_status_t status = up->GetDispatcherAndRights(vmar_handle, &vmar, &vmar_rights);
    if (status != MX_OK)
        return status;

    // lookup the VMO dispatcher from handle
    fbl::RefPtr<VmObjectDispatcher> vmo;
    mx_rights_t vmo_rights;
    status = up->GetDispatcherAndRights(vmo_handle, &vmo, &vmo_rights);
    if (status != MX_OK)
        return status;

    // test to see if we should even be able to map this
    if (!(vmo_rights & MX_RIGHT_MAP))
        return MX_ERR_ACCESS_DENIED;

    if (!VmAddressRegionDispatcher::is_valid_mapping_protection(map_flags))
        return MX_ERR_INVALID_ARGS;

    bool do_map_range = false;
    if (map_flags & MX_VM_FLAG_MAP_RANGE) {
        do_map_range = true;
        map_flags &= ~MX_VM_FLAG_MAP_RANGE;
    }

    // Usermode is not allowed to specify these flags on mappings, though we may
    // set them below.
    if (map_flags & (MX_VM_FLAG_CAN_MAP_READ | MX_VM_FLAG_CAN_MAP_WRITE | MX_VM_FLAG_CAN_MAP_EXECUTE)) {
        return MX_ERR_INVALID_ARGS;
    }

    // Permissions allowed by both the VMO and the VMAR
    const bool can_read = (vmo_rights & MX_RIGHT_READ) && (vmar_rights & MX_RIGHT_READ);
    const bool can_write = (vmo_rights & MX_RIGHT_WRITE) && (vmar_rights & MX_RIGHT_WRITE);
    const bool can_exec = (vmo_rights & MX_RIGHT_EXECUTE) && (vmar_rights & MX_RIGHT_EXECUTE);

    // test to see if the requested mapping protections are allowed
    if ((map_flags & MX_VM_FLAG_PERM_READ) && !can_read)
        return MX_ERR_ACCESS_DENIED;
    if ((map_flags & MX_VM_FLAG_PERM_WRITE) && !can_write)
        return MX_ERR_ACCESS_DENIED;
    if ((map_flags & MX_VM_FLAG_PERM_EXECUTE) && !can_exec)
        return MX_ERR_ACCESS_DENIED;

    // If a permission is allowed by both the VMO and the VMAR, add it to the
    // flags for the new mapping, so that the VMO's rights as of now can be used
    // to constrain future permission changes via Protect().
    if (can_read)
        map_flags |= MX_VM_FLAG_CAN_MAP_READ;
    if (can_write)
        map_flags |= MX_VM_FLAG_CAN_MAP_WRITE;
    if (can_exec)
        map_flags |= MX_VM_FLAG_CAN_MAP_EXECUTE;

    fbl::RefPtr<VmMapping> vm_mapping;
    status = vmar->Map(vmar_offset, vmo->vmo(), vmo_offset, len, map_flags, &vm_mapping);
    if (status != MX_OK)
        return status;

    // Setup a handler to destroy the new mapping if the syscall is unsuccessful.
    auto cleanup_handler = fbl::MakeAutoCall([vm_mapping]() {
        vm_mapping->Destroy();
    });

    if (do_map_range) {
        status = vm_mapping->MapRange(vmo_offset, len, false);
        if (status != MX_OK) {
            return status;
        }
    }

    if (_mapped_addr.copy_to_user(vm_mapping->base()) != MX_OK)
        return MX_ERR_INVALID_ARGS;

    cleanup_handler.cancel();
    return MX_OK;
}

mx_status_t sys_vmar_unmap(mx_handle_t vmar_handle, uintptr_t addr, size_t len) {
    auto up = ProcessDispatcher::GetCurrent();

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_status_t status = up->GetDispatcher(vmar_handle, &vmar);
    if (status != MX_OK)
        return status;

    return vmar->Unmap(addr, len);
}

mx_status_t sys_vmar_protect(mx_handle_t vmar_handle, uintptr_t addr, size_t len, uint32_t prot) {
    auto up = ProcessDispatcher::GetCurrent();

    mx_rights_t vmar_rights = 0u;
    if (prot & MX_VM_FLAG_PERM_READ)
        vmar_rights |= MX_RIGHT_READ;
    if (prot & MX_VM_FLAG_PERM_WRITE)
        vmar_rights |= MX_RIGHT_WRITE;
    if (prot & MX_VM_FLAG_PERM_EXECUTE)
        vmar_rights |= MX_RIGHT_EXECUTE;

    // lookup the dispatcher from handle
    fbl::RefPtr<VmAddressRegionDispatcher> vmar;
    mx_status_t status = up->GetDispatcherWithRights(vmar_handle, vmar_rights, &vmar);
    if (status != MX_OK)
        return status;

    if (!VmAddressRegionDispatcher::is_valid_mapping_protection(prot))
        return MX_ERR_INVALID_ARGS;

    return vmar->Protect(addr, len, prot);
}
