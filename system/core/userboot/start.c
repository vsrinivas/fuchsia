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

#include <zircon/stack.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>
#include <runtime/message.h>
#include <runtime/processargs.h>
#include <stdalign.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/param.h>
#include <zircon/syscalls/system.h>

#pragma GCC visibility pop

#define SHUTDOWN_COMMAND "poweroff"
#define STACK_VMO_NAME "userboot-child-initial-stack"

static noreturn void do_shutdown(zx_handle_t log, zx_handle_t rroot) {
    printl(log, "Process exited.  Executing \"" SHUTDOWN_COMMAND "\".");
    zx_system_powerctl(rroot, ZX_SYSTEM_POWERCTL_SHUTDOWN, NULL);
    printl(log, "still here after shutdown!");
    while (true)
        __builtin_trap();
}

static void load_child_process(zx_handle_t log,
                               const struct options* o, struct bootfs* bootfs,
                               zx_handle_t vdso_vmo, zx_handle_t proc,
                               zx_handle_t vmar, zx_handle_t thread,
                               zx_handle_t to_child,
                               zx_vaddr_t* entry, zx_vaddr_t* vdso_base,
                               size_t* stack_size, zx_handle_t* loader_svc) {
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
static zx_handle_t reserve_low_address_space(zx_handle_t log,
                                             zx_handle_t root_vmar) {
    zx_info_vmar_t info;
    check(log, zx_object_get_info(root_vmar, ZX_INFO_VMAR,
                                  &info, sizeof(info), NULL, NULL),
          "zx_object_get_info failed on child root VMAR handle");
    zx_handle_t vmar;
    uintptr_t addr;
    size_t reserve_size =
        (((info.base + info.len) / 2) + PAGE_SIZE - 1) & -PAGE_SIZE;
    zx_status_t status = zx_vmar_allocate(root_vmar, 0,
                                          reserve_size - info.base,
                                          ZX_VM_FLAG_SPECIFIC, &vmar, &addr);
    check(log, status,
          "zx_vmar_allocate failed for low address space reservation");
    if (addr != info.base)
        fail(log, "zx_vmar_allocate gave wrong address?!?");
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
// 4. Load up a channel with the zx_proc_args_t message for the child.
// 5. Start the child process running.
// 6. Optionally, wait for it to exit and then shut down.
static noreturn void bootstrap(zx_handle_t log, zx_handle_t bootstrap_pipe) {
    // Sample the bootstrap message to see how big it is.
    uint32_t nbytes;
    uint32_t nhandles;

    zx_status_t status = zxr_message_size(bootstrap_pipe, &nbytes, &nhandles);
    check(log, status, "zxr_message_size failed on bootstrap pipe!");

    // Read the bootstrap message from the kernel.
    ZXR_PROCESSARGS_BUFFER(buffer,
                           nbytes + EXTRA_HANDLE_COUNT * sizeof(uint32_t));
    zx_handle_t handles[nhandles + EXTRA_HANDLE_COUNT];
    zx_proc_args_t* pargs;
    uint32_t* handle_info;
    status = zxr_processargs_read(bootstrap_pipe,
                                  buffer, nbytes, handles, nhandles,
                                  &pargs, &handle_info);
    check(log, status, "zxr_processargs_read failed on bootstrap message!");

    // All done with the channel from the kernel now.  Let it go.
    zx_handle_close(bootstrap_pipe);

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
    status = zxr_processargs_strings(buffer, nbytes, NULL, environ, NULL);
    check(log, status,
          "zxr_processargs_strings failed on bootstrap message");

    // Process the kernel command line, which gives us options and also
    // becomes the environment strings for our child.
    struct options o;
    parse_options(log, &o, environ);

    zx_handle_t resource_root = ZX_HANDLE_INVALID;
    zx_handle_t bootdata_vmo = ZX_HANDLE_INVALID;
    zx_handle_t vdso_vmo = ZX_HANDLE_INVALID;
    zx_handle_t job = ZX_HANDLE_INVALID;
    zx_handle_t* proc_handle_loc = NULL;
    zx_handle_t* vmar_root_handle_loc = NULL;
    zx_handle_t* thread_handle_loc = NULL;
    zx_handle_t* stack_vmo_handle_loc = NULL;
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
            if (bootdata_vmo == ZX_HANDLE_INVALID) {
                bootdata_vmo = handles[i];
                zx_object_set_property(bootdata_vmo, ZX_PROP_NAME, "bootdata", 8);
            }
            break;
        }
    }
    if (vdso_vmo == ZX_HANDLE_INVALID)
        fail(log, "no vDSO handle in bootstrap message");
    if (resource_root == ZX_HANDLE_INVALID)
        fail(log, "no resource handle in bootstrap message");
    if (job == ZX_HANDLE_INVALID)
        fail(log, "no job handle in bootstrap message");
    if (vmar_root_handle_loc == NULL)
        fail(log, "no vmar root handle in bootstrap message");
    if (bootdata_vmo == ZX_HANDLE_INVALID)
        fail(log, "no bootdata VMO in bootstrap message");

    // Hang on to our own process handle.  If we closed it, our process
    // would be killed.  Exiting will clean it up.
    __UNUSED const zx_handle_t proc_self = *proc_handle_loc;
    const zx_handle_t vmar_self = *vmar_root_handle_loc;

    // Hang on to the resource root handle.
    zx_handle_t root_resource_handle;
    status = zx_handle_duplicate(resource_root, ZX_RIGHT_SAME_RIGHTS,
                                 &root_resource_handle);
    if (status < 0)
        fail(log, "zx_handle_duplicate failed: %d", status);

    // Locate the first bootfs bootdata section and decompress it.
    // We need it to load devmgr and libc from.
    // Later bootfs sections will be processed by devmgr.
    zx_handle_t bootfs_vmo = bootdata_get_bootfs(log, vmar_self, bootdata_vmo);

    // Pass the decompressed bootfs VMO on.
    handles[nhandles + EXTRA_HANDLE_BOOTFS] = bootfs_vmo;
    handle_info[nhandles + EXTRA_HANDLE_BOOTFS] =
        PA_HND(PA_VMO_BOOTFS, 0);

    // Map in the bootfs so we can look for files in it.
    struct bootfs bootfs;
    bootfs_mount(vmar_self, log, bootfs_vmo, &bootfs);

    // Make the channel for the bootstrap message.
    zx_handle_t to_child;
    zx_handle_t child_start_handle;
    status = zx_channel_create(0, &to_child, &child_start_handle);
    check(log, status, "zx_channel_create failed");

    const char* filename = o.value[OPTION_FILENAME];
    zx_handle_t proc;
    zx_handle_t vmar;
    status = zx_process_create(job, filename, strlen(filename), 0,
                               &proc, &vmar);
    if (status < 0)
        fail(log, "zx_process_create failed: %d", status);

    zx_handle_t reserve_vmar = reserve_low_address_space(log, vmar);

    // Create the initial thread in the new process
    zx_handle_t thread;
    status = zx_thread_create(proc, filename, strlen(filename), 0, &thread);
    if (status < 0)
        fail(log, "zx_thread_create failed: %d", status);

    zx_vaddr_t entry, vdso_base;
    size_t stack_size = ZIRCON_DEFAULT_STACK_SIZE;
    zx_handle_t loader_service_channel = ZX_HANDLE_INVALID;
    load_child_process(log, &o, &bootfs, vdso_vmo, proc, vmar,
                       thread, to_child, &entry, &vdso_base, &stack_size,
                       &loader_service_channel);

    // Allocate the stack for the child.
    stack_size = (stack_size + PAGE_SIZE - 1) & -PAGE_SIZE;
    zx_handle_t stack_vmo;
    status = zx_vmo_create(stack_size, 0, &stack_vmo);
    if (status < 0)
        fail(log, "zx_vmo_create failed for child stack: %d", status);
    zx_object_set_property(stack_vmo, ZX_PROP_NAME,
                           STACK_VMO_NAME, sizeof(STACK_VMO_NAME) - 1);
    zx_vaddr_t stack_base;
    status = zx_vmar_map(vmar, 0, stack_vmo, 0, stack_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                         &stack_base);
    check(log, status, "zx_vmar_map failed for child stack");
    uintptr_t sp = compute_initial_stack_pointer(stack_base, stack_size);
    if (stack_vmo_handle_loc != NULL) {
        // This is our own stack VMO handle, but we don't need it for anything.
        if (*stack_vmo_handle_loc != ZX_HANDLE_INVALID)
            zx_handle_close(*stack_vmo_handle_loc);
        *stack_vmo_handle_loc = stack_vmo;
    } else {
        zx_handle_close(stack_vmo);
    }

    // We're done doing mappings, so clear out the reservation VMAR.
    check(log, zx_vmar_destroy(reserve_vmar),
          "zx_vmar_destroy failed on reservation VMAR handle");
    check(log, zx_handle_close(reserve_vmar),
          "zx_handle_close failed on reservation VMAR handle");

    // Reuse the slot for the child's handle.
    status = zx_handle_duplicate(proc, ZX_RIGHT_SAME_RIGHTS, proc_handle_loc);
    if (status < 0)
        fail(log, "zx_handle_duplicate failed on child process handle: %d", status);

    if (thread_handle_loc != NULL) {
        // Reuse the slot for the child's handle.
        // NOTE: Leaks the current thread handle the same way as the process handle.
        status = zx_handle_duplicate(thread, ZX_RIGHT_SAME_RIGHTS,
                                     thread_handle_loc);
        if (status < 0)
            fail(log, "zx_handle_duplicate failed on child thread handle: %d", status);
    }

    // Reuse the slot for the child's root VMAR handle.  We don't need to hold
    // a reference to this, so just pass ours to the child.
    *vmar_root_handle_loc = vmar;

    // Now send the bootstrap message, consuming both our VMO handles. We also
    // send the job handle, which in the future means that we can't create more
    // processes from here on.
    status = zx_channel_write(to_child, 0, buffer, nbytes,
                              handles, nhandles + EXTRA_HANDLE_COUNT);
    check(log, status, "zx_channel_write to child failed");
    status = zx_handle_close(to_child);
    check(log, status, "zx_handle_close failed on channel handle");

    // Start the process going.
    status = zx_process_start(proc, thread, entry, sp,
                              child_start_handle, vdso_base);
    check(log, status, "zx_process_start failed");
    status = zx_handle_close(thread);
    check(log, status, "zx_handle_close failed on thread handle");

    printl(log, "process %s started.", o.value[OPTION_FILENAME]);

    // Now become the loader service for as long as that's needed.
    if (loader_service_channel != ZX_HANDLE_INVALID)
        loader_service(log, &bootfs, loader_service_channel);

    // All done with bootfs!
    bootfs_unmount(vmar_self, log, &bootfs);

    if (o.value[OPTION_SHUTDOWN] != NULL) {
        printl(log, "Waiting for %s to exit...", o.value[OPTION_FILENAME]);
        status = zx_object_wait_one(
            proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL);
        check(log, status, "zx_object_wait_one on process failed");
        do_shutdown(log, root_resource_handle);
    }

    // Now we've accomplished our purpose in life, and we can die happy.

    status = zx_handle_close(proc);
    check(log, status, "zx_handle_close failed on process handle");

    printl(log, "finished!");
    zx_process_exit(0);
}

// This is the entry point for the whole show, the very first bit of code
// to run in user mode.
noreturn void _start(void* start_arg) {
    zx_handle_t log = ZX_HANDLE_INVALID;
    zx_log_create(0, &log);
    if (log == ZX_HANDLE_INVALID)
        printl(log, "zx_log_create failed, using zx_debug_write instead");

    zx_handle_t bootstrap_pipe = (uintptr_t)start_arg;
    bootstrap(log, bootstrap_pipe);
}
