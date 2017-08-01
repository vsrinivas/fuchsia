// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mini-process/mini-process.h>

#include <dlfcn.h>
#include <stdint.h>

#include <elfload/elfload.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/stack.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>

#include "subprocess.h"

static void* get_syscall_addr(const void* syscall_fn, uintptr_t vdso_base) {
    Dl_info dl_info;
    if (dladdr(syscall_fn, &dl_info) == 0)
        return 0;
    return (void*)(vdso_base + ((uintptr_t)dl_info.dli_saddr - (uintptr_t)dl_info.dli_fbase));
}

static mx_status_t write_ctx_message(
    mx_handle_t channel, uintptr_t vdso_base, mx_handle_t transferred_handle) {
    minip_ctx_t ctx = {
        .handle_close = get_syscall_addr(&mx_handle_close, vdso_base),
        .object_wait_one = get_syscall_addr(&mx_object_wait_one, vdso_base),
        .object_signal = get_syscall_addr(&mx_object_signal, vdso_base),
        .event_create = get_syscall_addr(&mx_event_create, vdso_base),
        .channel_create = get_syscall_addr(&mx_channel_create, vdso_base),
        .channel_read = get_syscall_addr(&mx_channel_read, vdso_base),
        .channel_write = get_syscall_addr(&mx_channel_write, vdso_base),
        .process_exit = get_syscall_addr(&mx_process_exit, vdso_base)
    };
    return mx_channel_write(channel, 0u, &ctx, sizeof(ctx), &transferred_handle, 1u);
}

mx_status_t start_mini_process_etc(mx_handle_t process, mx_handle_t thread,
                                   mx_handle_t vmar,
                                   mx_handle_t transferred_handle,
                                   mx_handle_t* control_channel) {
    // Allocate a single VMO for the child. It doubles as the stack on the top and
    // as the executable code (minipr_thread_loop()) at the bottom. In theory, actual
    // stack usage is minimal, like 160 bytes or less.
    uint64_t stack_size = 16 * 1024u;
    mx_handle_t stack_vmo = MX_HANDLE_INVALID;
    mx_status_t status = mx_vmo_create(stack_size, 0, &stack_vmo);
    if (status != MX_OK)
        return status;
    // Try to set the name, but ignore any errors since it's purely for
    // debugging and diagnostics.
    static const char vmo_name[] = "mini-process:stack";
    mx_object_set_property(stack_vmo, MX_PROP_NAME, vmo_name, sizeof(vmo_name));

    // We assume that the code to execute is less than kSizeLimit bytes.
    const uint32_t kSizeLimit = 1000;
    size_t actual;
    status = mx_vmo_write(stack_vmo, &minipr_thread_loop, 0u, kSizeLimit,
                          &actual);
    if (status != MX_OK)
        goto exit;

    mx_vaddr_t stack_base;
    uint32_t perms = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE;
    status = mx_vmar_map(vmar, 0, stack_vmo, 0, stack_size, perms, &stack_base);
    if (status != MX_OK)
        goto exit;

    // Compute a valid starting SP for the machine's ABI.
    uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);
    mx_handle_t chn[2] = { MX_HANDLE_INVALID, MX_HANDLE_INVALID };


    if (!control_channel) {
        // Simple mode /////////////////////////////////////////////////////////////
        // Don't map the VDSO, so the only thing the mini-process can do is busy-loop.
        // The handle sent to the process is just the caller's handle.
        status = mx_process_start(process, thread, stack_base, sp, transferred_handle, 0);

    } else {
        // Complex mode ////////////////////////////////////////////////////////////
        // The mini-process is going to run a simple request-response over a channel
        // So we need to:
        // 1- map the VDSO in the child process, without launchpad.
        // 2- create a channel and give one end to the child process.
        // 3- send a message with the rest of the syscall function addresses.
        // 4- wait for reply.

        status = mx_channel_create(0u, &chn[0], &chn[1]);
        if (status != MX_OK)
            goto exit;

        // This is not thread-safe.  It steals the startup handle, so it's not
        // compatible with also using launchpad (which also needs to steal the
        // startup handle).
        static mx_handle_t vdso_vmo = MX_HANDLE_INVALID;
        if (vdso_vmo == MX_HANDLE_INVALID) {
            vdso_vmo = mx_get_startup_handle(PA_HND(PA_VMO_VDSO, 0));
            if (vdso_vmo == MX_HANDLE_INVALID) {
                status = MX_ERR_INTERNAL;
                goto exit;
            }
        }

        uintptr_t vdso_base = 0;
        elf_load_header_t header;
        uintptr_t phoff;
        mx_status_t status = elf_load_prepare(vdso_vmo, NULL, 0,
                                              &header, &phoff);
        if (status == MX_OK) {
            elf_phdr_t phdrs[header.e_phnum];
            status = elf_load_read_phdrs(vdso_vmo, phdrs, phoff,
                                         header.e_phnum);
            if (status == MX_OK)
                status = elf_load_map_segments(vmar, &header, phdrs, vdso_vmo,
                                               NULL, &vdso_base, NULL);
        }
        if (status != MX_OK)
            goto exit;

        status = write_ctx_message(chn[0], vdso_base, transferred_handle);
        if (status != MX_OK)
            goto exit;

        uintptr_t channel_read = (uintptr_t)get_syscall_addr(&mx_channel_read, vdso_base);

        status = mx_process_start(process, thread, stack_base, sp, chn[1], channel_read);
        if (status != MX_OK)
            goto exit;

        chn[1] = MX_HANDLE_INVALID;

        uint32_t observed;
        status = mx_object_wait_one(chn[0],
            MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, &observed);

        if (observed & MX_CHANNEL_PEER_CLOSED) {
            // the child process died prematurely.
            status = MX_ERR_UNAVAILABLE;
            goto exit;
        }

        if (observed & MX_CHANNEL_READABLE) {
            uint32_t ack[2];
            uint32_t actual_handles;
            uint32_t actual_bytes;
            status = mx_channel_read(
                chn[0], 0u, ack, NULL, sizeof(uint32_t) * 2, 0u, &actual_bytes, &actual_handles);
        }

        *control_channel = chn[0];
        chn[0] = MX_HANDLE_INVALID;
    }

exit:
    if (stack_vmo != MX_HANDLE_INVALID)
        mx_handle_close(stack_vmo);
    if (chn[0] != MX_HANDLE_INVALID)
        mx_handle_close(chn[0]);
    if (chn[1] != MX_HANDLE_INVALID)
        mx_handle_close(chn[1]);

    return status;
}

mx_status_t mini_process_cmd_send(mx_handle_t cntrl_channel, uint32_t what) {
    minip_cmd_t cmd = {
        .what = what,
        .status = MX_OK
    };

    return mx_channel_write(cntrl_channel, 0, &cmd, sizeof(cmd), NULL, 0);
}

mx_status_t mini_process_cmd_read_reply(mx_handle_t cntrl_channel,
                                        mx_handle_t* handle) {
    mx_status_t status = mx_object_wait_one(
        cntrl_channel, MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
        MX_TIME_INFINITE, NULL);
    if (status != MX_OK)
        return status;
    minip_cmd_t reply;
    uint32_t handle_count = handle ? 1 : 0;
    uint32_t actual_bytes = 0;
    uint32_t actual_handles = 0;
    status = mx_channel_read(cntrl_channel, 0, &reply, handle, sizeof(reply),
                             handle_count, &actual_bytes, &actual_handles);
    if (status != MX_OK)
        return status;
    return reply.status;
}

mx_status_t mini_process_cmd(mx_handle_t cntrl_channel, uint32_t what, mx_handle_t* handle) {
    mx_status_t status = mini_process_cmd_send(cntrl_channel, what);
    if (status != MX_OK)
        return status;
    return mini_process_cmd_read_reply(cntrl_channel, handle);
}

mx_status_t start_mini_process(mx_handle_t job, mx_handle_t transferred_handle,
                               mx_handle_t* process, mx_handle_t* thread) {
    *process = MX_HANDLE_INVALID;
    mx_handle_t vmar = MX_HANDLE_INVALID;
    mx_handle_t channel = MX_HANDLE_INVALID;

    mx_status_t status = mx_process_create(job, "minipr", 6u, 0u, process, &vmar);
    if (status != MX_OK)
        goto exit;

    *thread = MX_HANDLE_INVALID;
    status = mx_thread_create(*process, "minith", 6u, 0, thread);
    if (status != MX_OK)
        goto exit;

    status = start_mini_process_etc(*process, *thread, vmar, transferred_handle, &channel);
    // On success the transferred_handle gets consumed.
exit:
    if (status != MX_OK) {
        if (transferred_handle != MX_HANDLE_INVALID)
            mx_handle_close(transferred_handle);
        if (*process != MX_HANDLE_INVALID)
            mx_handle_close(*process);
        if (*thread != MX_HANDLE_INVALID)
            mx_handle_close(*thread);
    }

    if (channel != MX_HANDLE_INVALID)
        mx_handle_close(channel);

    return status;
}
