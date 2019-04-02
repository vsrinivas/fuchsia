// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/vector.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <lib/fdio/spawn.h>
#include <lib/zircon-internal/paths.h>
#include <lib/zx/channel.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <utility>

#include "fdio.h"

namespace devmgr {

#define CHILD_JOB_RIGHTS (ZX_RIGHTS_BASIC | ZX_RIGHT_MANAGE_JOB | ZX_RIGHT_MANAGE_PROCESS)

// clang-format off

static struct {
    const char* mount;
    const char* name;
    uint32_t flags;
} FSTAB[] = {
    { "/svc",       "svc",       FS_SVC },
    { "/hub",       "hub",       FS_HUB },
    { "/bin",       "bin",       FS_BIN },
    { "/dev",       "dev",       FS_DEV },
    { "/boot",      "boot",      FS_BOOT },
    { "/data",      "data",      FS_DATA },
    { "/system",    "system",    FS_SYSTEM },
    { "/install",   "install",   FS_INSTALL },
    { "/volume",    "volume",    FS_VOLUME },
    { "/blob",      "blob",      FS_BLOB },
    { "/pkgfs",     "pkgfs",     FS_PKGFS },
    { "/tmp",       "tmp",       FS_TMP },
};

// clang-format on
//
void devmgr_disable_appmgr_services() {
    FSTAB[1].flags = 0;
}

zx_status_t devmgr_launch_with_loader(const zx::job& job, const char* name, zx::vmo executable,
                                      zx::channel loader, const char* const* argv,
                                      const char** initial_envp, int stdiofd,
                                      const zx_handle_t* handles, const uint32_t* types,
                                      size_t hcount, zx::process* out_proc, uint32_t flags) {
    zx::job job_copy;
    zx_status_t status = job.duplicate(CHILD_JOB_RIGHTS, &job_copy);
    if (status != ZX_OK) {
        printf("devmgr_launch failed %s\n", zx_status_get_string(status));
        return status;
    }

    zx::debuglog debuglog;
    if (stdiofd < 0) {
        if ((status = zx::debuglog::create(zx::resource(), 0, &debuglog) != ZX_OK)) {
            return status;
        }
    }

    uint32_t spawn_flags = FDIO_SPAWN_CLONE_JOB;

    // Set up the environ for the new process
    fbl::Vector<const char*> env;
    if (getenv(LDSO_TRACE_CMDLINE)) {
        env.push_back(LDSO_TRACE_ENV);
    }
    env.push_back(ZX_SHELL_ENV_PATH);
    while (initial_envp && initial_envp[0]) {
        env.push_back(*initial_envp++);
    }
    env.push_back(nullptr);

    fbl::Vector<fdio_spawn_action_t> actions;
    actions.reserve(3 + fbl::count_of(FSTAB) + hcount);

    actions.push_back((fdio_spawn_action_t){
        .action = FDIO_SPAWN_ACTION_SET_NAME,
        .name = { .data = name },
    });

    if (loader.is_valid()) {
        actions.push_back((fdio_spawn_action_t){
            .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
            .h = { .id = PA_HND(PA_LDSVC_LOADER, 0), .handle = loader.release() },
        });
    } else {
        spawn_flags |= FDIO_SPAWN_DEFAULT_LDSVC;
    }

    // create namespace based on FS_* flags
    for (unsigned n = 0; n < fbl::count_of(FSTAB); n++) {
        if (!(FSTAB[n].flags & flags)) {
            continue;
        }
        zx_handle_t h;
        if ((h = fs_clone(FSTAB[n].name).release()) != ZX_HANDLE_INVALID) {
            actions.push_back((fdio_spawn_action_t){
                .action = FDIO_SPAWN_ACTION_ADD_NS_ENTRY,
                .ns = { .prefix = FSTAB[n].mount, .handle = h },
            });
        }
    }

    if (debuglog.is_valid()) {
        actions.push_back((fdio_spawn_action_t){
            .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
            .h = { .id = PA_HND(PA_FD, FDIO_FLAG_USE_FOR_STDIO | 0),
                   .handle = debuglog.release() },
        });
    } else {
        actions.push_back((fdio_spawn_action_t){
            .action = FDIO_SPAWN_ACTION_TRANSFER_FD,
            .fd = { .local_fd = stdiofd, .target_fd = FDIO_FLAG_USE_FOR_STDIO | 0 },
        });
    }

    for (size_t i = 0; i < hcount; ++i) {
        actions.push_back((fdio_spawn_action_t){
            .action = FDIO_SPAWN_ACTION_ADD_HANDLE,
            .h = { .id = types[i], .handle = handles[i] },
        });
    }

    zx::process proc;
    char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
    if (executable.is_valid()) {
        status = fdio_spawn_vmo(job_copy.get(), spawn_flags, executable.release(), argv,
                                env.get(), actions.size(), actions.get(),
                                proc.reset_and_get_address(), err_msg);
    } else {
        status = fdio_spawn_etc(job_copy.get(), spawn_flags, argv[0], argv,
                                env.get(), actions.size(), actions.get(),
                                proc.reset_and_get_address(), err_msg);
    }
    if (status != ZX_OK) {
        printf("devcoordinator: spawn %s (%s) failed: %s: %d\n", argv[0], name, err_msg, status);
        return status;
    }
    printf("devcoordinator: launch %s (%s) OK\n", argv[0], name);
    if (out_proc != nullptr) {
        *out_proc = std::move(proc);
    }
    return ZX_OK;
}

zx_status_t devmgr_launch(const zx::job& job, const char* name,
                          const char* const* argv, const char** initial_envp, int stdiofd,
                          const zx_handle_t* handles, const uint32_t* types, size_t hcount,
                          zx::process* out_proc, uint32_t flags) {
    return devmgr_launch_with_loader(job, name, zx::vmo(), zx::channel(), argv, initial_envp,
                                     stdiofd, handles, types, hcount, out_proc, flags);
}

ArgumentVector ArgumentVector::FromCmdline(const char* cmdline) {
    ArgumentVector argv;
    const size_t cmdline_len = strlen(cmdline) + 1;
    argv.raw_bytes_.reset(new char[cmdline_len]);
    memcpy(argv.raw_bytes_.get(), cmdline, cmdline_len);

    // Get the full commandline by splitting on '+'.
    size_t argc = 0;
    char* token;
    char* rest = argv.raw_bytes_.get();
    while (argc < fbl::count_of(argv.argv_) && (token = strtok_r(rest, "+", &rest))) {
        argv.argv_[argc++] = token;
    }
    argv.argv_[argc] = nullptr;
    return argv;
}

void ArgumentVector::Print(const char* prefix) const {
    const char* const* argv = argv_;
    printf("%s: starting", prefix);
    for (const char* arg = *argv; arg != nullptr; ++argv, arg = *argv) {
        printf(" '%s'", *argv);
    }
    printf("...\n");
}

} // namespace devmgr
