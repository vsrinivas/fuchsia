// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootfs.h"
#include "decompress.h"
#include "userboot-elf.h"
#include "option.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <magenta/stack.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <runtime/message.h>
#include <runtime/processargs.h>
#include <stdalign.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/param.h>

#pragma GCC visibility pop

#define SHUTDOWN_COMMAND "poweroff"

static noreturn void do_shutdown(mx_handle_t log, mx_handle_t rroot) {
    print(log, "Process exited.  Executing \"", SHUTDOWN_COMMAND, "\".\n",
          NULL);
    mx_debug_send_command(rroot, SHUTDOWN_COMMAND, strlen(SHUTDOWN_COMMAND));
    print(log, "still here after shutdown!\n", NULL);
    while (true)
        __builtin_trap();
}

static void load_child_process(mx_handle_t log, mx_handle_t vmar_self,
                               const struct options* o,
                               mx_handle_t bootfs_vmo, mx_handle_t vdso_vmo,
                               mx_handle_t proc, mx_handle_t vmar,
                               mx_handle_t to_child, mx_vaddr_t* entry,
                               mx_vaddr_t* vdso_base, size_t* stack_size) {

    // Examine the bootfs image and find the requested file in it.
    struct bootfs bootfs;
    bootfs_mount(vmar_self, log, bootfs_vmo, &bootfs);

    // This will handle a PT_INTERP by doing a second lookup in bootfs.
    *entry = elf_load_bootfs(log, vmar_self, &bootfs, proc, vmar,
                             o->value[OPTION_FILENAME], to_child, stack_size);

    // All done with bootfs!
    bootfs_unmount(vmar_self, log, bootfs_vmo, &bootfs);

    // Now load the vDSO into the child, so it has access to system calls.
    *vdso_base = elf_load_vmo(log, vmar_self, vmar, vdso_vmo);
}

// This is the main logic:
// 1. Read the kernel's bootstrap message.
// 2. Load up the child process from ELF file(s) on the bootfs.
// 3. Create the initial thread and allocate a stack for it.
// 4. Load up a message pipe with the mx_proc_args_t message for the child.
// 5. Start the child process running.
// 6. Optionally, wait for it to exit and then shut down.
static noreturn void bootstrap(mx_handle_t log, mx_handle_t bootstrap_pipe) {
    // Sample the bootstrap message to see how big it is.
    uint32_t nbytes;
    uint32_t nhandles;

    mx_status_t status = mxr_message_size(bootstrap_pipe, &nbytes, &nhandles);
    check(log, status, "mxr_message_size failed on bootstrap pipe!\n");

    // Read the bootstrap message from the kernel.
    MXR_PROCESSARGS_BUFFER(buffer, nbytes);
    mx_handle_t handles[nhandles];
    mx_proc_args_t* pargs;
    uint32_t* handle_info;
    status = mxr_processargs_read(bootstrap_pipe,
                                  buffer, nbytes, handles, nhandles,
                                  &pargs, &handle_info);
    check(log, status, "mxr_processargs_read failed on bootstrap message!\n");

    // All done with the message pipe from the kernel now.  Let it go.
    mx_handle_close(bootstrap_pipe);

    // Extract the environment (aka kernel command line) strings.
    char* environ[pargs->environ_num + 1];
    status = mxr_processargs_strings(buffer, nbytes, NULL, environ);
    check(log, status,
          "mxr_processargs_strings failed on bootstrap message\n");

    // Process the kernel command line, which gives us options and also
    // becomes the environment strings for our child.
    struct options o;
    parse_options(log, &o, environ);

    mx_handle_t resource_root = MX_HANDLE_INVALID;
    mx_handle_t bootfs_vmo = MX_HANDLE_INVALID;
    mx_handle_t vdso_vmo = MX_HANDLE_INVALID;
    mx_handle_t job = MX_HANDLE_INVALID;
    mx_handle_t* proc_handle_loc = NULL;
    mx_handle_t* vmar_root_handle_loc = NULL;
    mx_handle_t* thread_handle_loc = NULL;
    mx_handle_t* stack_vmo_handle_loc = NULL;
    for (uint32_t i = 0; i < nhandles; ++i) {
        switch (MX_HND_INFO_TYPE(handle_info[i])) {
        case MX_HND_TYPE_VDSO_VMO:
            vdso_vmo = handles[i];
            break;
        case MX_HND_TYPE_BOOTFS_VMO:
            if (MX_HND_INFO_ARG(handle_info[i]) == 0)
                bootfs_vmo = handles[i];
            break;
        case MX_HND_TYPE_PROC_SELF:
            proc_handle_loc = &handles[i];
            break;
        case MX_HND_TYPE_VMAR_ROOT:
            vmar_root_handle_loc = &handles[i];
            break;
        case MX_HND_TYPE_THREAD_SELF:
            thread_handle_loc = &handles[i];
            break;
        case MX_HND_TYPE_STACK_VMO:
            stack_vmo_handle_loc = &handles[i];
            break;
        case MX_HND_TYPE_RESOURCE:
            resource_root = handles[i];
            break;
        case MX_HND_TYPE_JOB:
            job = handles[i];
            break;
        }
    }
    if (bootfs_vmo == MX_HANDLE_INVALID)
        fail(log, ERR_INVALID_ARGS, "no bootfs handle in bootstrap message\n");
    if (vdso_vmo == MX_HANDLE_INVALID)
        fail(log, ERR_INVALID_ARGS, "no vDSO handle in bootstrap message\n");
    if (resource_root == MX_HANDLE_INVALID)
        fail(log, ERR_INVALID_ARGS, "no resource handle in bootstrap message\n");
    if (job == MX_HANDLE_INVALID)
        fail(log, ERR_INVALID_ARGS, "no job handle in bootstrap message\n");
    if (vmar_root_handle_loc == NULL)
        fail(log, ERR_INVALID_ARGS, "no vmar root handle in bootstrap message\n");

    // Hang on to our own process handle.  If we closed it, our process
    // would be killed.  Exiting will clean it up.
    __UNUSED const mx_handle_t proc_self = *proc_handle_loc;
    const mx_handle_t vmar_self = *vmar_root_handle_loc;

    // Hang on to the resource root handle.
    mx_handle_t root_resource_handle;
    status = mx_handle_duplicate(resource_root, MX_RIGHT_SAME_RIGHTS, &root_resource_handle);
    if (status < 0)
        fail(log, status, "mx_handle_duplicate failed\n");

    // Decompress any bootfs VMOs if necessary
    for (uint32_t i = 0; i < nhandles; ++i) {
        if (MX_HND_INFO_TYPE(handle_info[i]) == MX_HND_TYPE_BOOTFS_VMO) {
            handles[i] = decompress_vmo(log, vmar_self, handles[i]);
            if (MX_HND_INFO_ARG(handle_info[i]) == 0) {
                bootfs_vmo = handles[i];
            }
        }
    }

    // Make the channel for the bootstrap message.
    mx_handle_t pipeh[2];
    status = mx_channel_create(0, &pipeh[0], &pipeh[1]);
    check(log, status, "mx_channel_create failed\n");
    mx_handle_t to_child = pipeh[0];
    mx_handle_t child_start_handle = pipeh[1];

    const char* filename = o.value[OPTION_FILENAME];
    mx_handle_t proc;
    mx_handle_t vmar;
    status = mx_process_create(job, filename, strlen(filename), 0, &proc, &vmar);
    if (status < 0)
        fail(log, status, "mx_process_create failed\n");

    mx_vaddr_t entry, vdso_base;
    size_t stack_size = MAGENTA_DEFAULT_STACK_SIZE;
    load_child_process(log, vmar_self, &o, bootfs_vmo, vdso_vmo, proc, vmar,
                       to_child, &entry, &vdso_base, &stack_size);

    // Allocate the stack for the child.
    stack_size = (stack_size + PAGE_SIZE - 1) & -PAGE_SIZE;
    mx_handle_t stack_vmo;
    status = mx_vmo_create(stack_size, 0, &stack_vmo);
    if (status < 0)
        fail(log, status, "mx_vmo_create failed for child stack\n");
    mx_vaddr_t stack_base;
    status = mx_vmar_map(vmar, 0, stack_vmo, 0, stack_size,
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &stack_base);
    check(log, status, "mx_vmar_map failed for child stack\n");
    uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);
    if (stack_vmo_handle_loc != NULL) {
        // This is our own stack VMO handle, but we don't need it for anything.
        if (*stack_vmo_handle_loc != MX_HANDLE_INVALID)
            mx_handle_close(*stack_vmo_handle_loc);
        *stack_vmo_handle_loc = stack_vmo;
    } else {
        mx_handle_close(stack_vmo);
    }

    // Reuse the slot for the child's handle.
    status = mx_handle_duplicate(proc, MX_RIGHT_SAME_RIGHTS, proc_handle_loc);
    if (status < 0)
        fail(log, status,
             "mx_handle_duplicate failed on child process handle\n");

    // Create the initial thread in the new process
    mx_handle_t thread;
    status = mx_thread_create(proc, filename, strlen(filename), 0, &thread);
    if (status < 0)
        fail(log, status, "mx_thread_create failed\n");

    if (thread_handle_loc != NULL) {
        // Reuse the slot for the child's handle.
        // NOTE: Leaks the current thread handle the same way as the process handle.
        status = mx_handle_duplicate(thread, MX_RIGHT_SAME_RIGHTS, thread_handle_loc);
        if (status < 0)
            fail(log, status,
                 "mx_handle_duplicate failed on child thread handle\n");
    }

    // Reuse the slot for the child's root VMAR handle.  We don't need to hold
    // a reference to this, so just pass ours to the child.
    *vmar_root_handle_loc = vmar;

    // Now send the bootstrap message, consuming both our VMO handles. We also
    // send the job handle, which in the future means that we can't create more
    // processes from here on.
    status = mx_channel_write(to_child, 0, buffer, nbytes, handles, nhandles);
    check(log, status, "mx_channel_write to child failed\n");
    status = mx_handle_close(to_child);
    check(log, status, "mx_handle_close failed on message pipe handle\n");

    // Start the process going.
    status = mx_process_start(proc, thread, entry, sp,
                              child_start_handle, vdso_base);
    check(log, status, "mx_process_start failed\n");
    status = mx_handle_close(thread);
    check(log, status, "mx_handle_close failed on thread handle\n");

    if (o.value[OPTION_SHUTDOWN] != NULL) {
        print(log, "Waiting for ", o.value[OPTION_FILENAME], " to exit...\n",
              NULL);
        status = mx_handle_wait_one(
            proc, MX_SIGNAL_SIGNALED, MX_TIME_INFINITE, NULL);
        check(log, status, "mx_handle_wait_one on process failed\n");
        do_shutdown(log, root_resource_handle);
    }

    // Now we've accomplished our purpose in life, and we can die happy.

    status = mx_handle_close(proc);
    check(log, status, "mx_handle_close failed on process handle\n");

    print(log, o.value[OPTION_FILENAME], " started.  userboot exiting.\n",
          NULL);
    mx_process_exit(0);
}

// This is the entry point for the whole show, the very first bit of code
// to run in user mode.
noreturn void _start(void* start_arg) {
    mx_handle_t log = mx_log_create(MX_LOG_FLAG_DEVMGR);
    if (log == MX_HANDLE_INVALID)
        print(log, "mx_log_create failed, using mx_debug_write instead\n",
              NULL);

    mx_handle_t bootstrap_pipe = (uintptr_t)start_arg;
    bootstrap(log, bootstrap_pipe);
}
