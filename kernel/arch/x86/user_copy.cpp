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

// called from assembly
extern "C" bool _x86_usercopy_can_read(const void *base, size_t len);
extern "C" bool _x86_usercopy_can_write(const void *base, size_t len);

static inline bool ac_flag(void)
{
    return x86_save_flags() & X86_FLAGS_AC;
}

status_t arch_copy_from_user(void *dst, const void *src, size_t len)
{
    DEBUG_ASSERT(!ac_flag());

    bool smap_avail = x86_feature_test(X86_FEATURE_SMAP);
    thread_t *thr = get_current_thread();
    status_t status = _x86_copy_from_user(dst, src, len, smap_avail,
                                          &thr->arch.page_fault_resume);

    DEBUG_ASSERT(!ac_flag());
    return status;
}

status_t arch_copy_to_user(void *dst, const void *src, size_t len)
{
    DEBUG_ASSERT(!ac_flag());

    bool smap_avail = x86_feature_test(X86_FEATURE_SMAP);
    thread_t *thr = get_current_thread();
    status_t status = _x86_copy_to_user(dst, src, len, smap_avail,
                                        &thr->arch.page_fault_resume);

    DEBUG_ASSERT(!ac_flag());
    return status;
}

static bool can_access(const void *base, size_t len, bool for_write)
{
    LTRACEF("can_access: base %p, len %zu\n", base, len);

    // We don't care about whether pages are actually mapped or what their
    // permissions are, as long as they are in the user address space.  We
    // rely on a page fault occurring if an actual permissions error occurs.
    DEBUG_ASSERT(x86_get_cr0() & X86_CR0_WP);
    return is_user_address_range((vaddr_t)base, len);
}

bool _x86_usercopy_can_read(const void *base, size_t len)
{
    return can_access(base, len, false);
}

bool _x86_usercopy_can_write(const void *base, size_t len)
{
    return can_access(base, len, true);
}
