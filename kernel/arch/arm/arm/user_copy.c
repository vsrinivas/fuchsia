// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/user_copy.h>
#include <arch/arm/user_copy.h>
#include <kernel/thread.h>

status_t arch_copy_from_user(void *dst, const void *src, size_t len)
{
    thread_t *thr = get_current_thread();
    return _arm_copy_from_user(dst, src, len, &thr->arch.data_fault_resume);
}

status_t arch_copy_to_user(void *dst, const void *src, size_t len)
{
    thread_t *thr = get_current_thread();
    return _arm_copy_to_user(dst, src, len, &thr->arch.data_fault_resume);
}
