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

static void* get_syscall_addr(const void* syscall_fn, uintptr_t vdso_base) {
    Dl_info dl_info;
    if (dladdr(syscall_fn, &dl_info) == 0)
        return 0;
    return (void*)(vdso_base + ((uintptr_t)dl_info.dli_saddr - (uintptr_t)dl_info.dli_fbase));
}

// This struct defines the first message that the child process gets.
typedef struct {
    __typeof(mx_object_wait_one)*   object_wait_one;
    __typeof(mx_object_signal)*     object_signal;
    __typeof(mx_event_create)*      event_create;
    __typeof(mx_channel_create)*    channel_create;
    __typeof(mx_channel_read)*      channel_read;
    __typeof(mx_channel_write)*     channel_write;
    __typeof(mx_process_exit)*      process_exit;
} minip_ctx_t;

// Subsequent messages and replies are of this format. The |what| parameter is
// transaction friendly so the client can use mx_channel_call().
typedef struct {
    mx_txid_t what;
    mx_status_t status;
} minip_cmd_t;

static mx_status_t write_ctx_message(
    mx_handle_t channel, uintptr_t vdso_base, mx_handle_t transfered_handle) {
    minip_ctx_t ctx = {
        .object_wait_one = get_syscall_addr(&mx_object_wait_one, vdso_base),
        .object_signal = get_syscall_addr(&mx_object_signal, vdso_base),
        .event_create = get_syscall_addr(&mx_event_create, vdso_base),
        .channel_create = get_syscall_addr(&mx_channel_create, vdso_base),
        .channel_read = get_syscall_addr(&mx_channel_read, vdso_base),
        .channel_write = get_syscall_addr(&mx_channel_write, vdso_base),
        .process_exit = get_syscall_addr(&mx_process_exit, vdso_base)
    };
    return mx_channel_write(channel, 0u, &ctx, sizeof(ctx), &transfered_handle, 1u);
}

// This function is the entire program that the child process will execute. It
// gets directly mapped into the child process via mx_vmo_write() so it must not
// reference any addressable entity outside it.
__NO_SAFESTACK static void minipr_thread_loop(mx_handle_t channel, uintptr_t fnptr) {
    if (fnptr == 0) {
        // In this mode we don't have a VDSO so we don't care what the handle is
        // and therefore we busy-loop. Unless external steps are taken this will
        // saturate one core.
        volatile uint32_t val = 1;
        while (val) {
            val += 2u;
        }
    } else {
        // In this mode we do have a VDSO but we are not a real ELF program so
        // we need to receive from the parent the address of the syscalls we can
        // use. So we can bootstrap, kernel has already transfered the address of
        // mx_channel_read() and the handle to one end of the channel which already
        // contains a message with the rest of the syscall addresses.
        __typeof(mx_channel_read)* read_fn = (__typeof(mx_channel_read)*)fnptr;

        uint32_t actual = 0u;
        uint32_t actual_handles = 0u;
        mx_handle_t handle[2];
        minip_ctx_t ctx;

        mx_status_t status = (*read_fn)(
                channel, 0u, &ctx, handle, sizeof(ctx), 1, &actual, &actual_handles);
        if ((status != NO_ERROR) || (actual != sizeof(ctx)))
            __builtin_trap();

        // The received handle in the |ctx| message does not have any use other than
        // keeping it alive until the process ends. We basically leak it.

        // Acknowledge the initial message.
        uint32_t ack[2] = { actual, actual_handles };
        status = ctx.channel_write(channel, 0u, ack, sizeof(uint32_t) * 2, NULL, 0u);
        if (status != NO_ERROR)
            __builtin_trap();

        do {
            // wait for the next message.
            status = ctx.object_wait_one(
                channel, MX_CHANNEL_READABLE, MX_TIME_INFINITE, &actual);
            if (status != NO_ERROR)
                break;

            minip_cmd_t cmd;
            status = ctx.channel_read(
                channel, 0u, &cmd, NULL,  sizeof(cmd), 0u, &actual, &actual_handles);

            // Execute one or more commands. After each we send a reply with the
            // result. If the command does not cause to crash or exit.
            uint32_t what = cmd.what;

            do {
                // This loop is convoluted. A simpler switch() statement
                // has the risk of being generated as a table lookup which
                // makes it likely it will reference the data section which
                // is outside the memory copied to the child.

                handle[0] = MX_HANDLE_INVALID;
                handle[1] = MX_HANDLE_INVALID;

                if (what & MINIP_CMD_ECHO_MSG) {
                    what &= ~MINIP_CMD_ECHO_MSG;
                    cmd.status = NO_ERROR;
                    goto reply;
                }
                if (what & MINIP_CMD_CREATE_EVENT) {
                    what &= ~MINIP_CMD_CREATE_EVENT;
                    cmd.status = ctx.event_create(0u, &handle[0]);
                    goto reply;
                }
                if (what & MINIP_CMD_CREATE_CHANNEL) {
                    what &= ~MINIP_CMD_CREATE_CHANNEL;
                    cmd.status = ctx.channel_create(0u, &handle[0], &handle[1]);
                    goto reply;
                }

                // Neither MINIP_CMD_BUILTIN_TRAP nor MINIP_CMD_EXIT_NORMAL send a
                // message so the client will get either MX_CHANNEL_PEER_CLOSED if
                // it's doing a wait or will get ERR_CALL_FAILED of it's doing a
                // channel_call().

                if (what & MINIP_CMD_BUILTIN_TRAP)
                    __builtin_trap();

                if (what & MINIP_CMD_EXIT_NORMAL)
                    ctx.process_exit(0);

                // Did not match any known message.
                cmd.status = ERR_WRONG_TYPE;
reply:
                actual_handles = (handle[0] == MX_HANDLE_INVALID) ? 0u : 1u;
                status = ctx.channel_write(
                    channel, 0u, &cmd, sizeof(cmd), handle, actual_handles);

                // Loop if there are more commands packed in |what|.
            } while (what);

        } while (status == NO_ERROR);
    }

    __builtin_trap();
}

mx_status_t start_mini_process_etc(mx_handle_t process, mx_handle_t thread,
                                   mx_handle_t vmar,
                                   mx_handle_t transfered_handle,
                                   mx_handle_t* control_channel) {
    // Allocate a single VMO for the child. It doubles as the stack on the top and
    // as the executable code (minipr_thread_loop()) at the bottom. In theory, actual
    // stack usage is minimal, like 160 bytes or less.
    uint64_t stack_size = 16 * 1024u;
    mx_handle_t stack_vmo = MX_HANDLE_INVALID;
    mx_status_t status = mx_vmo_create(stack_size, 0, &stack_vmo);
    if (status != NO_ERROR)
        return status;

    // We assume that the code to execute is less than 600 bytes. As of gcc 6
    // the code is 488 bytes with frame pointers in x86 and a bit larger for ARM
    // and the stack usage is 152 bytes.
    size_t actual;
    status = mx_vmo_write(stack_vmo, &minipr_thread_loop, 0u, 600u, &actual);
    if (status != NO_ERROR)
        goto exit;

    mx_vaddr_t stack_base;
    uint32_t perms = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_PERM_EXECUTE;
    status = mx_vmar_map(vmar, 0, stack_vmo, 0, stack_size, perms, &stack_base);
    if (status != NO_ERROR)
        goto exit;

    // Compute a valid starting SP for the machine's ABI.
    uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);
    mx_handle_t chn[2] = { MX_HANDLE_INVALID, MX_HANDLE_INVALID };


    if (!control_channel) {
        // Simple mode /////////////////////////////////////////////////////////////
        // Don't map the VDSO, so the only thing the mini-process can do is busy-loop.
        // The handle sent to the process is just the caller's handle.
        status = mx_process_start(process, thread, stack_base, sp, transfered_handle, 0);

    } else {
        // Complex mode ////////////////////////////////////////////////////////////
        // The mini-process is going to run a simple request-response over a channel
        // So we need to:
        // 1- map the VDSO in the child process, without launchpad.
        // 2- create a channel and give one end to the child process.
        // 3- send a message with the rest of the syscall function addresses.
        // 4- wait for reply.

        status = mx_channel_create(0u, &chn[0], &chn[1]);
        if (status != NO_ERROR)
            goto exit;

        // This is not thread-safe.  It steals the startup handle, so it's not
        // compatible with also using launchpad (which also needs to steal the
        // startup handle).
        static mx_handle_t vdso_vmo = MX_HANDLE_INVALID;
        if (vdso_vmo == MX_HANDLE_INVALID) {
            vdso_vmo = mx_get_startup_handle(PA_HND(PA_VMO_VDSO, 0));
            if (vdso_vmo == MX_HANDLE_INVALID) {
                status = ERR_INTERNAL;
                goto exit;
            }
        }

        uintptr_t vdso_base = 0;
        elf_load_header_t header;
        uintptr_t phoff;
        mx_status_t status = elf_load_prepare(vdso_vmo, NULL, 0,
                                              &header, &phoff);
        if (status == NO_ERROR) {
            elf_phdr_t phdrs[header.e_phnum];
            status = elf_load_read_phdrs(vdso_vmo, phdrs, phoff,
                                         header.e_phnum);
            if (status == NO_ERROR)
                status = elf_load_map_segments(vmar, &header, phdrs, vdso_vmo,
                                               NULL, &vdso_base, NULL);
        }
        if (status != NO_ERROR)
            goto exit;

        status = write_ctx_message(chn[0], vdso_base, transfered_handle);
        if (status != NO_ERROR)
            goto exit;

        uintptr_t channel_read = (uintptr_t)get_syscall_addr(&mx_channel_read, vdso_base);

        status = mx_process_start(process, thread, stack_base, sp, chn[1], channel_read);
        if (status != NO_ERROR)
            goto exit;

        chn[1] = MX_HANDLE_INVALID;

        uint32_t observed;
        status = mx_object_wait_one(chn[0],
            MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED, MX_TIME_INFINITE, &observed);

        if (observed & MX_CHANNEL_PEER_CLOSED) {
            // the child process died prematurely.
            status = ERR_UNAVAILABLE;
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


mx_status_t mini_process_cmd(mx_handle_t cntrl_channel, uint32_t what, mx_handle_t* handle) {
    // We know that the process replies with the same |cmd.what| so we
    // can use the fancier channel_call() rather than wait + read.

    minip_cmd_t cmd = {
        .what = what,
        .status = NO_ERROR
    };

    mx_channel_call_args_t call_args = {
        .wr_bytes = &cmd,
        .wr_handles = NULL,
        .rd_bytes = &cmd,
        .rd_handles = handle,
        .wr_num_bytes = sizeof(minip_cmd_t),
        .wr_num_handles = 0u,
        .rd_num_bytes = sizeof(minip_cmd_t),
        .rd_num_handles = handle ? 1u : 0u
    };

    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    mx_status_t read_status = NO_ERROR;

    mx_status_t status = mx_channel_call(cntrl_channel,
        0u, MX_TIME_INFINITE, &call_args, &actual_bytes, &actual_handles, &read_status);

    if (status == ERR_CALL_FAILED)
        return read_status;
    else if (status != NO_ERROR)
        return status;
    // Message received. Return the status of the remote operation.
    return cmd.status;
}

mx_status_t start_mini_process(mx_handle_t job, mx_handle_t transfered_handle,
                               mx_handle_t* process, mx_handle_t* thread) {
    *process = MX_HANDLE_INVALID;
    mx_handle_t vmar = MX_HANDLE_INVALID;
    mx_handle_t channel = MX_HANDLE_INVALID;

    mx_status_t status = mx_process_create(job, "minipr", 6u, 0u, process, &vmar);
    if (status != NO_ERROR)
        goto exit;

    *thread = MX_HANDLE_INVALID;
    status = mx_thread_create(*process, "minith", 6u, 0, thread);
    if (status != NO_ERROR)
        goto exit;

    status = start_mini_process_etc(*process, *thread, vmar, transfered_handle, &channel);
    // On success the transfered_handle gets consumed.
exit:
    if (status != NO_ERROR) {
        if (transfered_handle != MX_HANDLE_INVALID)
            mx_handle_close(transfered_handle);
        if (*process != MX_HANDLE_INVALID)
            mx_handle_close(*process);
        if (*thread != MX_HANDLE_INVALID)
            mx_handle_close(*thread);
    }

    if (channel != MX_HANDLE_INVALID)
        mx_handle_close(channel);

    return status;
}
