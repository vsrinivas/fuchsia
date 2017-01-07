// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <magenta/syscalls.h>
#include <magenta/status.h>

static void __attribute__((optimize("O0"))) minipr_thread_loop(void) {
    volatile uint32_t val = 1;
    while (val) {
        // note that this loop never terminates and will staturate one core unless
        // you take additional steps.
        val += 2u;
    }
}

mx_status_t start_mini_process(mx_handle_t job, mx_handle_t* process, mx_handle_t* thread) {
    // Allocate a single VMO for the child. It doubles as the stack on the top and
    // as the executable code (minipr_thread_loop()) at the bottom. In theory the stack usage
    // is minimal, like 32 bytes or less.
    uint64_t stack_size = 16 * 1024u;
    mx_handle_t stack_vmo = MX_HANDLE_INVALID;
    mx_status_t status = mx_vmo_create(stack_size, 0, &stack_vmo);
    if (status < 0)
        return MX_HANDLE_INVALID;

    // We assume that the code to execute is less than 80 bytes. As of gcc 6
    // the code is 52 bytes with frame pointers in x86 and a bit larger for ARM.
    size_t actual;
    status = mx_vmo_write(stack_vmo, &minipr_thread_loop, 0u, 80u, &actual);
    if (status < 0)
        return MX_HANDLE_INVALID;

    *process = MX_HANDLE_INVALID;
    mx_handle_t vmar = MX_HANDLE_INVALID;
    status = mx_process_create(job, "minipr", 6u, 0u, process, &vmar);
    if (status < 0)
        goto exit;

    *thread = MX_HANDLE_INVALID;
    status = mx_thread_create(*process, "minith", 6u, 0, thread);
    if (status < 0)
        goto exit;

    mx_vaddr_t stack_base;
    status = mx_vmar_map(vmar, 0, stack_vmo, 0, stack_size,
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE,
                         &stack_base);
    if (status < 0)
        goto exit;

    // See stack.h for explanation about this.
    uintptr_t sp = stack_size + stack_base - 8;

    // In theory any trasferable object will do but in the future we might require
    // this to be a channel.
    mx_handle_t chan[2] = {MX_HANDLE_INVALID, MX_HANDLE_INVALID};
    status = mx_channel_create(0, &chan[0], &chan[1]);
    if (status < 0)
        goto exit;

    status = mx_process_start(*process, *thread, stack_base, sp, chan[1], 0u);

    // If succesful, one end has been transfered therefore our handle is invalid.
    if (status == NO_ERROR)
        chan[1] = MX_HANDLE_INVALID;

exit:
    if (vmar != MX_HANDLE_INVALID)
        mx_handle_close(vmar);
    if (stack_vmo != MX_HANDLE_INVALID)
        mx_handle_close(stack_vmo);
    if (chan[0] != MX_HANDLE_INVALID)
        mx_handle_close(chan[0]);
    if (chan[1] != MX_HANDLE_INVALID)
        mx_handle_close(chan[1]);

    if (status < 0) {
        if (*process != MX_HANDLE_INVALID)
            mx_handle_close(*process);
        if (*thread != MX_HANDLE_INVALID)
            mx_handle_close(*thread);
    }

    return status;
}
