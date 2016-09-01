// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "userboot-elf.h"

#include "bootfs.h"
#include "util.h"

#pragma GCC visibility push(hidden)

#include <elf.h>
#include <elfload/elfload.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <stdbool.h>
#include <string.h>

#pragma GCC visibility pop

#define INTERP_PREFIX "lib/"

// TODO(mcgrathr): Remove proc_self when elf_load_map_segments no longer uses it.
static mx_vaddr_t load(mx_handle_t log, mx_handle_t proc_self,
                       mx_handle_t proc, mx_handle_t vmo,
                       uintptr_t* interp_off, size_t* interp_len,
                       size_t* stack_size, bool close_vmo, bool return_entry) {
    elf_load_header_t header;
    uintptr_t phoff;
    mx_status_t status = elf_load_prepare(vmo, &header, &phoff);
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
    status = elf_load_map_segments(proc_self, proc, &header, phdrs, vmo,
                                   return_entry ? NULL : &addr,
                                   return_entry ? &addr : NULL);
    check(log, status, "elf_load_map_segments failed\n");

    if (close_vmo)
        mx_handle_close(vmo);

    return addr;
}

mx_vaddr_t elf_load_vmo(mx_handle_t log, mx_handle_t proc_self,
                        mx_handle_t proc, mx_handle_t vmo) {
    return load(log, proc_self, proc, vmo, NULL, NULL, NULL, false, false);
}

enum loader_bootstrap_handle_index {
    BOOTSTRAP_EXEC_VMO,
    BOOTSTRAP_LOGGER,
    BOOTSTRAP_PROC,
    BOOTSTRAP_HANDLES
};

#define LOADER_BOOTSTRAP_ENVIRON "LD_DEBUG=1"
#define LOADER_BOOTSTRAP_ENVIRON_NUM 1

struct loader_bootstrap_message {
    mx_proc_args_t header;
    uint32_t handle_info[BOOTSTRAP_HANDLES];
    char env[sizeof(LOADER_BOOTSTRAP_ENVIRON)];
};

static void stuff_loader_bootstrap(mx_handle_t log, mx_handle_t proc,
                                   mx_handle_t to_child, mx_handle_t vmo) {
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
            [BOOTSTRAP_EXEC_VMO] = MX_HND_INFO(MX_HND_TYPE_EXEC_VMO, 0),
            [BOOTSTRAP_LOGGER] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, 0),
            [BOOTSTRAP_PROC] = MX_HND_INFO(MX_HND_TYPE_PROC_SELF, 0),
        },
        .env = LOADER_BOOTSTRAP_ENVIRON,
    };
    mx_handle_t handles[] = {
        [BOOTSTRAP_EXEC_VMO] = vmo,
        [BOOTSTRAP_LOGGER] = mx_handle_duplicate(log, MX_RIGHT_SAME_RIGHTS),
        [BOOTSTRAP_PROC] = mx_handle_duplicate(proc, MX_RIGHT_SAME_RIGHTS),
    };

    mx_status_t status = mx_msgpipe_write(
        to_child, &msg, sizeof(msg),
        handles, sizeof(handles) / sizeof(handles[0]), 0);
    check(log, status,
          "mx_msgpipe_write of loader bootstrap message failed\n");
}

mx_vaddr_t elf_load_bootfs(mx_handle_t log, mx_handle_t proc_self,
                           struct bootfs *fs, mx_handle_t proc,
                           const char* filename, mx_handle_t to_child,
                           size_t* stack_size) {
    mx_handle_t vmo = bootfs_open(log, fs, filename);

    uintptr_t interp_off = 0;
    size_t interp_len = 0;
    mx_vaddr_t entry = load(log, proc_self, proc, vmo, &interp_off, &interp_len,
                            stack_size, true, true);
    if (interp_len > 0) {
        char interp[sizeof(INTERP_PREFIX) + interp_len];
        memcpy(interp, INTERP_PREFIX, sizeof(INTERP_PREFIX) - 1);
        mx_ssize_t n = mx_vmo_read(
            vmo, &interp[sizeof(INTERP_PREFIX) - 1], interp_off, interp_len);
        if (n < 0)
            fail(log, n, "mx_vmo_read failed\n");
        if (n != (mx_ssize_t)interp_len)
            fail(log, ERR_ELF_BAD_FORMAT, "mx_vmo_read short read\n");
        interp[sizeof(INTERP_PREFIX) - 1 + interp_len] = '\0';

        print(log, filename, " has PT_INTERP \"", interp, "\"\n", NULL);

        stuff_loader_bootstrap(log, proc, to_child, vmo);

        mx_handle_t interp_vmo = bootfs_open(log, fs, interp);
        entry = load(log, proc_self, proc,
                     interp_vmo, NULL, NULL, NULL, true, true);
    }
    return entry;
}
