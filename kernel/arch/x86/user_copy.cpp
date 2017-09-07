// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <string.h>
#include <trace.h>

#include <arch/user_copy.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/user_copy.h>
#include <kernel/thread.h>
#include <kernel/vm.h>

#define LOCAL_TRACE 0

static inline bool ac_flag(void)
{
    return x86_save_flags() & X86_FLAGS_AC;
}

static bool can_access(const void *base, size_t len)
{
    LTRACEF("can_access: base %p, len %zu\n", base, len);

    // We don't care about whether pages are actually mapped or what their
    // permissions are, as long as they are in the user address space.  We
    // rely on a page fault occurring if an actual permissions error occurs.
    DEBUG_ASSERT(x86_get_cr0() & X86_CR0_WP);
    return is_user_address_range((vaddr_t)base, len);
}

status_t arch_copy_from_user(void *dst, const void *src, size_t len)
{
    DEBUG_ASSERT(!ac_flag());

    if (!can_access(src, len))
        return MX_ERR_INVALID_ARGS;

    thread_t *thr = get_current_thread();
    status_t status = _x86_copy_to_or_from_user(dst, src, len,
                                                &thr->arch.page_fault_resume);

    DEBUG_ASSERT(!ac_flag());
    return status;
}

status_t arch_copy_to_user(void *dst, const void *src, size_t len)
{
    DEBUG_ASSERT(!ac_flag());

    if (!can_access(dst, len))
        return MX_ERR_INVALID_ARGS;

    thread_t *thr = get_current_thread();
    status_t status = _x86_copy_to_or_from_user(dst, src, len,
                                                &thr->arch.page_fault_resume);

    DEBUG_ASSERT(!ac_flag());
    return status;
}
