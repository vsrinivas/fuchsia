// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/user_copy.h>
#include <arch/user_copy.h>
#include <kernel/thread.h>
#include <vm/vm.h>

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
    // The assembly code just does memcpy with fault handling.  This is
    // the security check that an address from the user is actually a
    // valid userspace address so users can't access kernel memory.
    if (!is_user_address_range((vaddr_t)src, len)) {
        return ZX_ERR_INVALID_ARGS;
    }

    return _arm64_user_copy(dst, src, len,
                            &get_current_thread()->arch.data_fault_resume);
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
    if (!is_user_address_range((vaddr_t)dst, len)) {
        return ZX_ERR_INVALID_ARGS;
    }

    return _arm64_user_copy(dst, src, len,
                            &get_current_thread()->arch.data_fault_resume);
}
