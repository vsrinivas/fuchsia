// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/mmu.h>
#include <vm/vm_aspace.h>

// page fault handler for second level address translation.
mx_status_t vmm_guest_page_fault_handler(vaddr_t va, uint flags, fbl::RefPtr<VmAspace> paspace);
