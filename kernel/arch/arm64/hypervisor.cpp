// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <magenta/errors.h>

status_t arch_hypervisor_create(mxtl::unique_ptr<HypervisorContext>* context) {
    return ERR_NOT_SUPPORTED;
}

status_t arch_guest_create(mxtl::RefPtr<VmObject> phys_mem,
                           mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                           mxtl::unique_ptr<GuestContext>* context) {
    return ERR_NOT_SUPPORTED;
}

status_t arch_guest_enter(const mxtl::unique_ptr<GuestContext>& context) {
    return ERR_NOT_SUPPORTED;
}

status_t arch_guest_set_entry(const mxtl::unique_ptr<GuestContext>& context,
                              uintptr_t guest_entry) {
    return ERR_NOT_SUPPORTED;
}
