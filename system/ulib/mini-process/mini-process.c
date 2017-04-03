// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <magenta/syscalls.h>
#include <magenta/status.h>
#include <magenta/stack.h>

static void __attribute__((optimize("O0"))) minipr_thread_loop(void) {
    volatile uint32_t val = 1;
    while (val) {
        // note that this loop never terminates and will staturate one core unless
        // you take additional steps.
        val += 2u;
    }
}

mx_status_t start_mini_process_etc(mx_handle_t process, mx_handle_t thread,
                                   mx_handle_t vmar, mx_handle_t transfered_handle) {
    // Allocate a single VMO for the child. It doubles as the stack on the top and
    // as the executable code (minipr_thread_loop()) at the bottom. In theory the stack usage
    // is minimal, like 32 bytes or less.
    uint64_t stack_size = 16 * 1024u;
    mx_handle_t stack_vmo = MX_HANDLE_INVALID;
    mx_status_t status = mx_vmo_create(stack_size, 0, &stack_vmo);
    if (status < 0)
        return status;

    // We assume that the code to execute is less than 80 bytes. As of gcc 6
    // the code is 52 bytes with frame pointers in x86 and a bit larger for ARM.
    size_t actual;
    status = mx_vmo_write(stack_vmo, &minipr_thread_loop, 0u, 80u, &actual);
    if (status < 0)
        return status;

    mx_vaddr_t stack_base;
    status = mx_vmar_map(vmar, 0, stack_vmo, 0, stack_size,
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE,
                         &stack_base);
    if (status < 0)
        goto exit;

    // Compute a valid starting SP for the machine's ABI.
    uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);

    status = mx_process_start(process, thread, stack_base, sp, transfered_handle, 0u);

exit:
    if (stack_vmo != MX_HANDLE_INVALID)
        mx_handle_close(stack_vmo);

    return status;
}

mx_status_t start_mini_process(mx_handle_t job, mx_handle_t transfered_handle,
                               mx_handle_t* process, mx_handle_t* thread) {
    *process = MX_HANDLE_INVALID;
    mx_handle_t vmar = MX_HANDLE_INVALID;
    mx_status_t status = mx_process_create(job, "minipr", 6u, 0u, process, &vmar);
    if (status < 0)
        goto exit;

    *thread = MX_HANDLE_INVALID;
    status = mx_thread_create(*process, "minith", 6u, 0, thread);
    if (status < 0)
        goto exit;

    status = start_mini_process_etc(*process, *thread, vmar, transfered_handle);

    // On sucess the transfered_handle gets consumed.

    if (status == NO_ERROR) {
        // We wait 10ms here to make sure that the thread has transitioned
        // to into active. This is flaky but might make tests less flaky.
        // TODO(cpu): investigate signals for this job.
        mx_nanosleep(mx_deadline_after(MX_MSEC(10)));
    }

exit:
    if (status < 0) {
        if (transfered_handle != MX_HANDLE_INVALID)
            mx_handle_close(transfered_handle);
        if (*process != MX_HANDLE_INVALID)
            mx_handle_close(*process);
        if (*thread != MX_HANDLE_INVALID)
            mx_handle_close(*thread);
    }

    return status;
}
