// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <magenta/errors.h>
#include <vm/vm_object.h>

// static
mx_status_t Guest::Create(mxtl::RefPtr<VmObject> physmem, mxtl::unique_ptr<Guest>* out) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t arch_guest_create(mxtl::RefPtr<VmObject> physmem, mxtl::unique_ptr<Guest>* guest) {
    if (arm64_get_boot_el() < 2)
        return MX_ERR_NOT_SUPPORTED;

    return Guest::Create(mxtl::move(physmem), guest);
}

mx_status_t arch_guest_set_trap(Guest* guest, uint32_t kind, mx_vaddr_t addr, size_t len,
                                mxtl::RefPtr<PortDispatcher> port, uint64_t key) {
    return MX_ERR_NOT_SUPPORTED;
}
