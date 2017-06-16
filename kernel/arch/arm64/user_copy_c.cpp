// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <string.h>
#include <trace.h>

#include <arch/user_copy.h>
#include <arch/arm64/user_copy.h>
#include <kernel/thread.h>
#include <kernel/vm.h>

#define LOCAL_TRACE 0

status_t arch_copy_from_user(void *dst, const void *src, size_t len)
{
    // We check if the address is a user pointer, since when ldtr/sttr
    // cause a data fault, they do not indicate if the fault was caused
    // by an access attempt to a privileged address.  Because of this, we
    // don't know if the user copy failed due to being a bad address or
    // if we just need to run the page fault handler and try again.  This
    // check lets us skip the ambiguity and always try to run the page fault
    // handler (since the fault will always be from trying to access an
    // unprivileged page).
    if (!is_user_address_range((vaddr_t)src, len)) {
        return MX_ERR_INVALID_ARGS;
    }

    thread_t *thr = get_current_thread();
    status_t status =
            _arm64_copy_from_user(dst, src, len, &thr->arch.data_fault_resume);
    return status;
}

status_t arch_copy_to_user(void *dst, const void *src, size_t len)
{
    if (!is_user_address_range((vaddr_t)dst, len)) {
        return MX_ERR_INVALID_ARGS;
    }

    thread_t *thr = get_current_thread();
    status_t status =
            _arm64_copy_to_user(dst, src, len, &thr->arch.data_fault_resume);
    return status;
}
