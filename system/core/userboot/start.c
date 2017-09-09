// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootdata.h"
#include "bootfs.h"
#include "loader-service.h"
#include "option.h"
#include "userboot-elf.h"
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
#define STACK_VMO_NAME "userboot-child-initial-stack"

static noreturn void do_shutdown(mx_handle_t log, mx_handle_t rroot) {
    printl(log, "Process exited.  Executing \"" SHUTDOWN_COMMAND "\".");
    mx_debug_send_command(rroot, SHUTDOWN_COMMAND, strlen(SHUTDOWN_COMMAND));
    printl(log, "still here after shutdown!");
    while (true)
        __builtin_trap();
}

static void load_child_process(mx_handle_t log,
                               const struct options* o, struct bootfs* bootfs,
                               mx_handle_t vdso_vmo, mx_handle_t proc,
                               mx_handle_t vmar, mx_handle_t thread,
                               mx_handle_t to_child,
                               mx_vaddr_t* entry, mx_vaddr_t* vdso_base,
                               size_t* stack_size, mx_handle_t* loader_svc) {
    // Examine the bootfs image and find the requested file in it.
    // This will handle a PT_INTERP by doing a second lookup in bootfs.
    *entry = elf_load_bootfs(log, bootfs, proc, vmar, thread,
                             o->value[OPTION_FILENAME], to_child, stack_size,
                             loader_svc);

    // Now load the vDSO into the child, so it has access to system calls.
    *vdso_base = elf_load_vmo(log, vmar, vdso_vmo);
}

// Reserve roughly the low half of the address space, so the initial
// process can use sanitizers that need to allocate shadow memory there.
// The reservation VMAR is kept around just long enough to make sure all
// the initial allocations (mapping in the initial ELF object, and
// allocating the initial stack) stay out of this area, and then destroyed.
// The process's own allocations can then use the full address space; if
// it's using a sanitizer, it will set up its shadow memory first thing.
static mx_handle_t reserve_low_address_space(mx_handle_t log,
                                             mx_handle_t root_vmar) {
    mx_info_vmar_t info;
    check(log, mx_object_get_info(root_vmar, MX_INFO_VMAR,
                                  &info, sizeof(info), NULL, NULL),
          "mx_object_get_info failed on child root VMAR handle");
    mx_handle_t vmar;
    uintptr_t addr;
    size_t reserve_size =
        (((info.base + info.len) / 2) + PAGE_SIZE - 1) & -PAGE_SIZE;
    mx_status_t status = mx_vmar_allocate(root_vmar, 0,
                                          reserve_size - info.base,
                                          MX_VM_FLAG_SPECIFIC, &vmar, &addr);
    check(log, status,
          "mx_vmar_allocate failed for low address space reservation");
    if (addr != info.base)
        fail(log, "mx_vmar_allocate gave wrong address?!?");
    return vmar;
}

enum {
    EXTRA_HANDLE_BOOTFS,
    EXTRA_HANDLE_COUNT
};

// This is the main logic:
// 1. Read the kernel's bootstrap message.
// 2. Load up the child process from ELF file(s) on the bootfs.
// 3. Create the initial thread and allocate a stack for it.
// 4. Load up a channel with the mx_proc_args_t message for the child.
// 5. Start the child process running.
// 6. Optionally, wait for it to exit and then shut down.
static noreturn void bootstrap(mx_handle_t log, mx_handle_t bootstrap_pipe) {
    // Sample the bootstrap message to see how big it is.
    uint32_t nbytes;
    uint32_t nhandles;

    mx_status_t status = mxr_message_size(bootstrap_pipe, &nbytes, &nhandles);
    check(log, status, "mxr_message_size failed on bootstrap pipe!");

    // Read the bootstrap message from the kernel.
    MXR_PROCESSARGS_BUFFER(buffer,
                           nbytes + EXTRA_HANDLE_COUNT * sizeof(uint32_t));
    mx_handle_t handles[nhandles + EXTRA_HANDLE_COUNT];
    mx_proc_args_t* pargs;
    uint32_t* handle_info;
    status = mxr_processargs_read(bootstrap_pipe,
                                  buffer, nbytes, handles, nhandles,
                                  &pargs, &handle_info);
    check(log, status, "mxr_processargs_read failed on bootstrap message!");

    // All done with the channel from the kernel now.  Let it go.
    mx_handle_close(bootstrap_pipe);

    // We're adding some extra handles, so we have to rearrange the
    // incoming message buffer to make space for their info slots.
    if (pargs->args_off != 0 || pargs->args_num != 0) {
        fail(log, "unexpected bootstrap message layout: args");
    }
    if (pargs->environ_off != (pargs->handle_info_off +
                               nhandles * sizeof(uint32_t))) {
        fail(log, "unexpected bootstrap message layout: environ");
    }
    const size_t environ_size = nbytes - pargs->environ_off;
    pargs->environ_off += EXTRA_HANDLE_COUNT * sizeof(uint32_t);
    memmove(&buffer[pargs->environ_off],
            &buffer[pargs->handle_info_off + nhandles * sizeof(uint32_t)],
            environ_size);
    nbytes += EXTRA_HANDLE_COUNT * sizeof(uint32_t);

    // Extract the environment (aka kernel command line) strings.
    char* environ[pargs->environ_num + 1];
    status = mxr_processargs_strings(buffer, nbytes, NULL, environ, NULL);
    check(log, status,
          "mxr_processargs_strings failed on bootstrap message");

    // Process the kernel command line, which gives us options and also
    // becomes the environment strings for our child.
    struct options o;
    parse_options(log, &o, environ);

    mx_handle_t resource_root = MX_HANDLE_INVALID;
    mx_handle_t bootdata_vmo = MX_HANDLE_INVALID;
    mx_handle_t vdso_vmo = MX_HANDLE_INVALID;
    mx_handle_t job = MX_HANDLE_INVALID;
    mx_handle_t* proc_handle_loc = NULL;
    mx_handle_t* vmar_root_handle_loc = NULL;
    mx_handle_t* thread_handle_loc = NULL;
    mx_handle_t* stack_vmo_handle_loc = NULL;
    for (uint32_t i = 0; i < nhandles; ++i) {
        switch (handle_info[i]) {
        case PA_HND(PA_VMO_VDSO, 0):
            vdso_vmo = handles[i];
            break;
        case PA_HND(PA_PROC_SELF, 0):
            proc_handle_loc = &handles[i];
            break;
        case PA_HND(PA_VMAR_ROOT, 0):
            vmar_root_handle_loc = &handles[i];
            break;
        case PA_HND(PA_THREAD_SELF, 0):
            thread_handle_loc = &handles[i];
            break;
        case PA_HND(PA_VMO_STACK, 0):
            stack_vmo_handle_loc = &handles[i];
            break;
        case PA_HND(PA_RESOURCE, 0):
            resource_root = handles[i];
            break;
        case PA_HND(PA_JOB_DEFAULT, 0):
            job = handles[i];
            break;
        case PA_HND(PA_VMO_BOOTDATA, 0):
            if (bootdata_vmo == MX_HANDLE_INVALID) {
                bootdata_vmo = handles[i];
                mx_object_set_property(bootdata_vmo, MX_PROP_NAME, "bootdata", 8);
            }
            break;
        }
    }
    if (vdso_vmo == MX_HANDLE_INVALID)
        fail(log, "no vDSO handle in bootstrap message");
    if (resource_root == MX_HANDLE_INVALID)
        fail(log, "no resource handle in bootstrap message");
    if (job == MX_HANDLE_INVALID)
        fail(log, "no job handle in bootstrap message");
    if (vmar_root_handle_loc == NULL)
        fail(log, "no vmar root handle in bootstrap message");
    if (bootdata_vmo == MX_HANDLE_INVALID)
        fail(log, "no bootdata VMO in bootstrap message");

    // Hang on to our own process handle.  If we closed it, our process
    // would be killed.  Exiting will clean it up.
    __UNUSED const mx_handle_t proc_self = *proc_handle_loc;
    const mx_handle_t vmar_self = *vmar_root_handle_loc;

    // Hang on to the resource root handle.
    mx_handle_t root_resource_handle;
    status = mx_handle_duplicate(resource_root, MX_RIGHT_SAME_RIGHTS,
                                 &root_resource_handle);
    if (status < 0)
        fail(log, "mx_handle_duplicate failed: %d", status);

    // Locate the first bootfs bootdata section and decompress it.
    // We need it to load devmgr and libc from.
    // Later bootfs sections will be processed by devmgr.
    mx_handle_t bootfs_vmo = bootdata_get_bootfs(log, vmar_self, bootdata_vmo);

    // Pass the decompressed bootfs VMO on.
    handles[nhandles + EXTRA_HANDLE_BOOTFS] = bootfs_vmo;
    handle_info[nhandles + EXTRA_HANDLE_BOOTFS] =
        PA_HND(PA_VMO_BOOTFS, 0);

    // Map in the bootfs so we can look for files in it.
    struct bootfs bootfs;
    bootfs_mount(vmar_self, log, bootfs_vmo, &bootfs);

    // Make the channel for the bootstrap message.
    mx_handle_t to_child;
    mx_handle_t child_start_handle;
    status = mx_channel_create(0, &to_child, &child_start_handle);
    check(log, status, "mx_channel_create failed");

    const char* filename = o.value[OPTION_FILENAME];
    mx_handle_t proc;
    mx_handle_t vmar;
    status = mx_process_create(job, filename, strlen(filename), 0,
                               &proc, &vmar);
    if (status < 0)
        fail(log, "mx_process_create failed: %d", status);

    mx_handle_t reserve_vmar = reserve_low_address_space(log, vmar);

    // Create the initial thread in the new process
    mx_handle_t thread;
    status = mx_thread_create(proc, filename, strlen(filename), 0, &thread);
    if (status < 0)
        fail(log, "mx_thread_create failed: %d", status);

    mx_vaddr_t entry, vdso_base;
    size_t stack_size = MAGENTA_DEFAULT_STACK_SIZE;
    mx_handle_t loader_service_channel = MX_HANDLE_INVALID;
    load_child_process(log, &o, &bootfs, vdso_vmo, proc, vmar,
                       thread, to_child, &entry, &vdso_base, &stack_size,
                       &loader_service_channel);

    // Allocate the stack for the child.
    stack_size = (stack_size + PAGE_SIZE - 1) & -PAGE_SIZE;
    mx_handle_t stack_vmo;
    status = mx_vmo_create(stack_size, 0, &stack_vmo);
    if (status < 0)
        fail(log, "mx_vmo_create failed for child stack: %d", status);
    mx_object_set_property(stack_vmo, MX_PROP_NAME,
                           STACK_VMO_NAME, sizeof(STACK_VMO_NAME) - 1);
    mx_vaddr_t stack_base;
    status = mx_vmar_map(vmar, 0, stack_vmo, 0, stack_size,
                         MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE,
                         &stack_base);
    check(log, status, "mx_vmar_map failed for child stack");
    uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);
    if (stack_vmo_handle_loc != NULL) {
        // This is our own stack VMO handle, but we don't need it for anything.
        if (*stack_vmo_handle_loc != MX_HANDLE_INVALID)
            mx_handle_close(*stack_vmo_handle_loc);
        *stack_vmo_handle_loc = stack_vmo;
    } else {
        mx_handle_close(stack_vmo);
    }

    // We're done doing mappings, so clear out the reservation VMAR.
    check(log, mx_vmar_destroy(reserve_vmar),
          "mx_vmar_destroy failed on reservation VMAR handle");
    check(log, mx_handle_close(reserve_vmar),
          "mx_handle_close failed on reservation VMAR handle");

    // Reuse the slot for the child's handle.
    status = mx_handle_duplicate(proc, MX_RIGHT_SAME_RIGHTS, proc_handle_loc);
    if (status < 0)
        fail(log, "mx_handle_duplicate failed on child process handle: %d", status);

    if (thread_handle_loc != NULL) {
        // Reuse the slot for the child's handle.
        // NOTE: Leaks the current thread handle the same way as the process handle.
        status = mx_handle_duplicate(thread, MX_RIGHT_SAME_RIGHTS,
                                     thread_handle_loc);
        if (status < 0)
            fail(log, "mx_handle_duplicate failed on child thread handle: %d", status);
    }

    // Reuse the slot for the child's root VMAR handle.  We don't need to hold
    // a reference to this, so just pass ours to the child.
    *vmar_root_handle_loc = vmar;

    // Now send the bootstrap message, consuming both our VMO handles. We also
    // send the job handle, which in the future means that we can't create more
    // processes from here on.
    status = mx_channel_write(to_child, 0, buffer, nbytes,
                              handles, nhandles + EXTRA_HANDLE_COUNT);
    check(log, status, "mx_channel_write to child failed");
    status = mx_handle_close(to_child);
    check(log, status, "mx_handle_close failed on channel handle");

    // Start the process going.
    status = mx_process_start(proc, thread, entry, sp,
                              child_start_handle, vdso_base);
    check(log, status, "mx_process_start failed");
    status = mx_handle_close(thread);
    check(log, status, "mx_handle_close failed on thread handle");

    printl(log, "process %s started.", o.value[OPTION_FILENAME]);

    // Now become the loader service for as long as that's needed.
    if (loader_service_channel != MX_HANDLE_INVALID)
        loader_service(log, &bootfs, loader_service_channel);

    // All done with bootfs!
    bootfs_unmount(vmar_self, log, &bootfs);

    if (o.value[OPTION_SHUTDOWN] != NULL) {
        printl(log, "Waiting for %s to exit...", o.value[OPTION_FILENAME]);
        status = mx_object_wait_one(
            proc, MX_PROCESS_TERMINATED, MX_TIME_INFINITE, NULL);
        check(log, status, "mx_object_wait_one on process failed");
        do_shutdown(log, root_resource_handle);
    }

    // Now we've accomplished our purpose in life, and we can die happy.

    status = mx_handle_close(proc);
    check(log, status, "mx_handle_close failed on process handle");

    printl(log, "finished!");
    mx_process_exit(0);
}

// This is the entry point for the whole show, the very first bit of code
// to run in user mode.
noreturn void _start(void* start_arg) {
    mx_handle_t log = MX_HANDLE_INVALID;
    mx_log_create(0, &log);
    if (log == MX_HANDLE_INVALID)
        printl(log, "mx_log_create failed, using mx_debug_write instead");

    mx_handle_t bootstrap_pipe = (uintptr_t)start_arg;
    bootstrap(log, bootstrap_pipe);
}
