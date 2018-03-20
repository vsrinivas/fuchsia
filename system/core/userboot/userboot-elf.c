// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "userboot-elf.h"

#include "bootfs.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <elf.h>
#include <elfload/elfload.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <stdbool.h>
#include <string.h>

#pragma GCC visibility pop

#define INTERP_PREFIX "lib/"

static zx_vaddr_t load(zx_handle_t log, const char* what,
                       zx_handle_t vmar, zx_handle_t vmo,
                       uintptr_t* interp_off, size_t* interp_len,
                       zx_handle_t* segments_vmar, size_t* stack_size,
                       bool close_vmo, bool return_entry) {
    elf_load_header_t header;
    uintptr_t phoff;
    zx_status_t status = elf_load_prepare(vmo, NULL, 0, &header, &phoff);
    check(log, status, "elf_load_prepare failed");

    elf_phdr_t phdrs[header.e_phnum];
    status = elf_load_read_phdrs(vmo, phdrs, phoff, header.e_phnum);
    check(log, status, "elf_load_read_phdrs failed");

    if (interp_off != NULL &&
        elf_load_find_interp(phdrs, header.e_phnum, interp_off, interp_len))
        return 0;

    if (stack_size != NULL) {
        for (size_t i = 0; i < header.e_phnum; ++i) {
            if (phdrs[i].p_type == PT_GNU_STACK && phdrs[i].p_memsz > 0)
                *stack_size = phdrs[i].p_memsz;
        }
    }

    zx_vaddr_t base, entry;
    status = elf_load_map_segments(vmar, &header, phdrs, vmo,
                                   segments_vmar, &base, &entry);
    check(log, status, "elf_load_map_segments failed");

    if (close_vmo)
        zx_handle_close(vmo);

    printl(log, "userboot: loaded %s at %p, entry point %p\n",
           what, (void*)base, (void*)entry);
    return return_entry ? entry : base;
}

zx_vaddr_t elf_load_vmo(zx_handle_t log, zx_handle_t vmar, zx_handle_t vmo) {
    return load(log, "vDSO", vmar, vmo, NULL, NULL, NULL, NULL, false, false);
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
    zx_proc_args_t header;
    uint32_t handle_info[BOOTSTRAP_HANDLES];
    char env[sizeof(LOADER_BOOTSTRAP_ENVIRON)];
};

static void stuff_loader_bootstrap(zx_handle_t log,
                                   zx_handle_t proc, zx_handle_t root_vmar,
                                   zx_handle_t thread,
                                   zx_handle_t to_child,
                                   zx_handle_t segments_vmar,
                                   zx_handle_t vmo,
                                   zx_handle_t* loader_svc) {
    struct loader_bootstrap_message msg = {
        .header = {
            .protocol = ZX_PROCARGS_PROTOCOL,
            .version = ZX_PROCARGS_VERSION,
            .handle_info_off = offsetof(struct loader_bootstrap_message,
                                        handle_info),
            .environ_num = LOADER_BOOTSTRAP_ENVIRON_NUM,
            .environ_off = offsetof(struct loader_bootstrap_message, env),
        },
        .handle_info = {
            [BOOTSTRAP_EXEC_VMO] = PA_HND(PA_VMO_EXECUTABLE, 0),
            [BOOTSTRAP_LOGGER] = PA_HND(PA_FDIO_LOGGER, 0),
            [BOOTSTRAP_PROC] = PA_HND(PA_PROC_SELF, 0),
            [BOOTSTRAP_ROOT_VMAR] = PA_HND(PA_VMAR_ROOT, 0),
            [BOOTSTRAP_SEGMENTS_VMAR] = PA_HND(PA_VMAR_LOADED, 0),
            [BOOTSTRAP_THREAD] = PA_HND(PA_THREAD_SELF, 0),
            [BOOTSTRAP_LOADER_SVC] = PA_HND(PA_SVC_LOADER, 0),
        },
        .env = LOADER_BOOTSTRAP_ENVIRON,
    };
    zx_handle_t handles[] = {
        [BOOTSTRAP_EXEC_VMO] = vmo,
        [BOOTSTRAP_LOGGER] = ZX_HANDLE_INVALID,
        [BOOTSTRAP_PROC] = ZX_HANDLE_INVALID,
        [BOOTSTRAP_ROOT_VMAR] = ZX_HANDLE_INVALID,
        [BOOTSTRAP_SEGMENTS_VMAR] = segments_vmar,
        [BOOTSTRAP_THREAD] = ZX_HANDLE_INVALID,
        [BOOTSTRAP_LOADER_SVC] = ZX_HANDLE_INVALID,
    };
    check(log, zx_handle_duplicate(log, ZX_RIGHT_SAME_RIGHTS,
                                   &handles[BOOTSTRAP_LOGGER]),
          "zx_handle_duplicate failed");
    check(log, zx_handle_duplicate(proc, ZX_RIGHT_SAME_RIGHTS,
                                   &handles[BOOTSTRAP_PROC]),
          "zx_handle_duplicate failed");
    check(log, zx_handle_duplicate(root_vmar, ZX_RIGHT_SAME_RIGHTS,
                                   &handles[BOOTSTRAP_ROOT_VMAR]),
          "zx_handle_duplicate failed");
    check(log, zx_handle_duplicate(thread, ZX_RIGHT_SAME_RIGHTS,
                                   &handles[BOOTSTRAP_THREAD]),
          "zx_handle_duplicate failed");
    check(log, zx_channel_create(0, loader_svc,
                                 &handles[BOOTSTRAP_LOADER_SVC]),
          "zx_channel_create failed");

    zx_status_t status = zx_channel_write(
        to_child, 0, &msg, sizeof(msg), handles, countof(handles));
    check(log, status,
          "zx_channel_write of loader bootstrap message failed");
}

zx_vaddr_t elf_load_bootfs(zx_handle_t log, struct bootfs *fs, zx_handle_t proc,
                           zx_handle_t vmar, zx_handle_t thread,
                           const char* filename, zx_handle_t to_child,
                           size_t* stack_size, zx_handle_t* loader_svc) {
    zx_handle_t vmo = bootfs_open(log, "program", fs, filename);

    uintptr_t interp_off = 0;
    size_t interp_len = 0;
    zx_vaddr_t entry = load(log, filename,
                            vmar, vmo, &interp_off, &interp_len,
                            NULL, stack_size, true, true);
    if (interp_len > 0) {
        char interp[sizeof(INTERP_PREFIX) + interp_len];
        memcpy(interp, INTERP_PREFIX, sizeof(INTERP_PREFIX) - 1);
        zx_status_t status = zx_vmo_read(
            vmo, &interp[sizeof(INTERP_PREFIX) - 1],
            interp_off, interp_len);
        if (status < 0)
            fail(log, "zx_vmo_read failed: %d", status);
        interp[sizeof(INTERP_PREFIX) - 1 + interp_len] = '\0';

        printl(log, "'%s' has PT_INTERP \"%s\"", filename, interp);

        zx_handle_t interp_vmo =
            bootfs_open(log, "dynamic linker", fs, interp);
        zx_handle_t interp_vmar;
        entry = load(log, interp, vmar, interp_vmo,
                     NULL, NULL, &interp_vmar, NULL, true, true);

        stuff_loader_bootstrap(log, proc, vmar, thread, to_child,
                               interp_vmar, vmo, loader_svc);
    }
    return entry;
}
