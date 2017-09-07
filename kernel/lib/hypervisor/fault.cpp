// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "hypervisor/fault.h"

mx_status_t vmm_guest_page_fault_handler(vaddr_t guest_paddr, uint flags,
                                         fbl::RefPtr<VmAspace> paspace) {
    return paspace->PageFault(guest_paddr, flags);
}
