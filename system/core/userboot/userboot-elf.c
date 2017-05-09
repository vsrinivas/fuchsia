// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "userboot-elf.h"

#include "bootfs.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <elf.h>
#include <elfload/elfload.h>
#include <magenta/compiler.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <stdbool.h>
#include <string.h>

#pragma GCC visibility pop

#define INTERP_PREFIX "lib/"

static mx_vaddr_t load(mx_handle_t log, mx_handle_t vmar, mx_handle_t vmo,
                       uintptr_t* interp_off, size_t* interp_len,
                       mx_handle_t* segments_vmar, size_t* stack_size,
                       bool close_vmo, bool return_entry) {
    elf_load_header_t header;
    uintptr_t phoff;
    mx_status_t status = elf_load_prepare(vmo, NULL, 0, &header, &phoff);
    check(log, status, "elf_load_prepare failed\n");

    elf_phdr_t phdrs[header.e_phnum];
    status = elf_load_read_phdrs(vmo, phdrs, phoff, header.e_phnum);
    check(log, status, "elf_load_read_phdrs failed\n");

    if (interp_off != NULL &&
        elf_load_find_interp(phdrs, header.e_phnum, interp_off, interp_len))
        return 0;

    if (stack_size != NULL) {
        for (size_t i = 0; i < header.e_phnum; ++i) {
            if (phdrs[i].p_type == PT_GNU_STACK && phdrs[i].p_memsz > 0)
                *stack_size = phdrs[i].p_memsz;
        }
    }

    mx_vaddr_t addr;
    status = elf_load_map_segments(vmar, &header, phdrs, vmo,
                                   segments_vmar,
                                   return_entry ? NULL : &addr,
                                   return_entry ? &addr : NULL);
    check(log, status, "elf_load_map_segments failed\n");

    if (close_vmo)
        mx_handle_close(vmo);

    return addr;
}

mx_vaddr_t elf_load_vmo(mx_handle_t log, mx_handle_t vmar, mx_handle_t vmo) {
    return load(log, vmar, vmo, NULL, NULL, NULL, NULL, false, false);
}

enum loader_bootstrap_handle_index {
    BOOTSTRAP_EXEC_VMO,
    BOOTSTRAP_LOGGER,
    BOOTSTRAP_PROC,
    BOOTSTRAP_ROOT_VMAR,
    BOOTSTRAP_SEGMENTS_VMAR,
    BOOTSTRAP_THREAD,
    BOOTSTRAP_LOADER_SVC,
    BOOTSTRAP_HANDLES
};

#define LOADER_BOOTSTRAP_ENVIRON "LD_DEBUG=1"
#define LOADER_BOOTSTRAP_ENVIRON_NUM 1

struct loader_bootstrap_message {
    mx_proc_args_t header;
    uint32_t handle_info[BOOTSTRAP_HANDLES];
    char env[sizeof(LOADER_BOOTSTRAP_ENVIRON)];
};

static void stuff_loader_bootstrap(mx_handle_t log,
                                   mx_handle_t proc, mx_handle_t root_vmar,
                                   mx_handle_t thread,
                                   mx_handle_t to_child,
                                   mx_handle_t segments_vmar,
                                   mx_handle_t vmo,
                                   mx_handle_t* loader_svc) {
    struct loader_bootstrap_message msg = {
        .header = {
            .protocol = MX_PROCARGS_PROTOCOL,
            .version = MX_PROCARGS_VERSION,
            .handle_info_off = offsetof(struct loader_bootstrap_message,
                                        handle_info),
            .environ_num = LOADER_BOOTSTRAP_ENVIRON_NUM,
            .environ_off = offsetof(struct loader_bootstrap_message, env),
        },
        .handle_info = {
            [BOOTSTRAP_EXEC_VMO] = PA_HND(PA_VMO_EXECUTABLE, 0),
            [BOOTSTRAP_LOGGER] = PA_HND(PA_MXIO_LOGGER, 0),
            [BOOTSTRAP_PROC] = PA_HND(PA_PROC_SELF, 0),
            [BOOTSTRAP_ROOT_VMAR] = PA_HND(PA_VMAR_ROOT, 0),
            [BOOTSTRAP_SEGMENTS_VMAR] = PA_HND(PA_VMAR_LOADED, 0),
            [BOOTSTRAP_THREAD] = PA_HND(PA_THREAD_SELF, 0),
            [BOOTSTRAP_LOADER_SVC] = PA_HND(PA_SVC_LOADER, 0),
        },
        .env = LOADER_BOOTSTRAP_ENVIRON,
    };
    mx_handle_t handles[] = {
        [BOOTSTRAP_EXEC_VMO] = vmo,
        [BOOTSTRAP_LOGGER] = MX_HANDLE_INVALID,
        [BOOTSTRAP_PROC] = MX_HANDLE_INVALID,
        [BOOTSTRAP_ROOT_VMAR] = MX_HANDLE_INVALID,
        [BOOTSTRAP_SEGMENTS_VMAR] = segments_vmar,
        [BOOTSTRAP_THREAD] = MX_HANDLE_INVALID,
        [BOOTSTRAP_LOADER_SVC] = MX_HANDLE_INVALID,
    };
    check(log, mx_handle_duplicate(log, MX_RIGHT_SAME_RIGHTS,
                                   &handles[BOOTSTRAP_LOGGER]),
          "mx_handle_duplicate failed\n");
    check(log, mx_handle_duplicate(proc, MX_RIGHT_SAME_RIGHTS,
                                   &handles[BOOTSTRAP_PROC]),
          "mx_handle_duplicate failed\n");
    check(log, mx_handle_duplicate(root_vmar, MX_RIGHT_SAME_RIGHTS,
                                   &handles[BOOTSTRAP_ROOT_VMAR]),
          "mx_handle_duplicate failed\n");
    check(log, mx_handle_duplicate(thread, MX_RIGHT_SAME_RIGHTS,
                                   &handles[BOOTSTRAP_THREAD]),
          "mx_handle_duplicate failed\n");
    check(log, mx_channel_create(0, loader_svc,
                                 &handles[BOOTSTRAP_LOADER_SVC]),
          "mx_channel_create failed\n");

    mx_status_t status = mx_channel_write(
        to_child, 0, &msg, sizeof(msg), handles, countof(handles));
    check(log, status,
          "mx_channel_write of loader bootstrap message failed\n");
}

mx_vaddr_t elf_load_bootfs(mx_handle_t log, struct bootfs *fs, mx_handle_t proc,
                           mx_handle_t vmar, mx_handle_t thread,
                           const char* filename, mx_handle_t to_child,
                           size_t* stack_size, mx_handle_t* loader_svc) {
    mx_handle_t vmo = bootfs_open(log, "program", fs, filename);

    uintptr_t interp_off = 0;
    size_t interp_len = 0;
    mx_vaddr_t entry = load(log, vmar, vmo, &interp_off, &interp_len,
                            NULL, stack_size, true, true);
    if (interp_len > 0) {
        char interp[sizeof(INTERP_PREFIX) + interp_len];
        memcpy(interp, INTERP_PREFIX, sizeof(INTERP_PREFIX) - 1);
        size_t n;
        mx_status_t status = mx_vmo_read(
            vmo, &interp[sizeof(INTERP_PREFIX) - 1],
            interp_off, interp_len, &n);
        if (status < 0)
            fail(log, status, "mx_vmo_read failed\n");
        if (n != interp_len)
            fail(log, ERR_ELF_BAD_FORMAT, "mx_vmo_read short read\n");
        interp[sizeof(INTERP_PREFIX) - 1 + interp_len] = '\0';

        print(log, filename, " has PT_INTERP \"", interp, "\"\n", NULL);

        mx_handle_t interp_vmo =
            bootfs_open(log, "dynamic linker", fs, interp);
        mx_handle_t interp_vmar;
        entry = load(log, vmar, interp_vmo,
                     NULL, NULL, &interp_vmar, NULL, true, true);

        stuff_loader_bootstrap(log, proc, vmar, thread, to_child,
                               interp_vmar, vmo, loader_svc);
    }
    return entry;
}
