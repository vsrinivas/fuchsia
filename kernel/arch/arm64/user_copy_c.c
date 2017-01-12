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
    thread_t *thr = get_current_thread();
    status_t status =
            _arm64_copy_from_user(dst, src, len, &thr->arch.data_fault_resume);
    return status;
}

status_t arch_copy_to_user(void *dst, const void *src, size_t len)
{
    thread_t *thr = get_current_thread();
    status_t status =
            _arm64_copy_to_user(dst, src, len, &thr->arch.data_fault_resume);
    return status;
}
