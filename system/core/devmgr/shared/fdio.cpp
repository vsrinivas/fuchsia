// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <fbl/algorithm.h>
#include <lib/fdio/io.h>
#include <lib/fdio/util.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmo.h>

#include <zircon/paths.h>
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

#define MAX_ENVP 16
#define CHILD_JOB_RIGHTS (ZX_RIGHTS_BASIC | ZX_RIGHT_MANAGE_JOB | ZX_RIGHT_MANAGE_PROCESS)

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

void devmgr_disable_appmgr_services() {
    FSTAB[1].flags = 0;
}

zx_status_t devmgr_launch(
    const zx::job& job, const char* name,
    zx_status_t (*load)(void*, launchpad_t*, const char*), void* ctx,
    int argc, const char* const* argv,
    const char** initial_envp, int stdiofd,
    const zx_handle_t* handles, const uint32_t* types, size_t hcount,
    zx::process* out_proc, uint32_t flags) {
    zx_status_t status;
    const char* envp[MAX_ENVP + 1];
    unsigned envn = 0;

    if (getenv(LDSO_TRACE_CMDLINE)) {
        envp[envn++] = LDSO_TRACE_ENV;
    }
    envp[envn++] = ZX_SHELL_ENV_PATH;
    while ((initial_envp && initial_envp[0]) && (envn < MAX_ENVP)) {
        envp[envn++] = *initial_envp++;
    }
    envp[envn++] = nullptr;

    zx::job job_copy;
    status = job.duplicate(CHILD_JOB_RIGHTS, &job_copy);
    if (status != ZX_OK) {
        printf("devmgr_launch failed %s\n", zx_status_get_string(status));
        return status;
    }

    launchpad_t* lp;
    launchpad_create(job_copy.get(), name, &lp);

    status = (*load)(ctx, lp, argv[0]);
    if (status != ZX_OK) {
        launchpad_abort(lp, status, "cannot load file");
    }
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);

    // create namespace based on FS_* flags
    const char* nametable[fbl::count_of(FSTAB)] = { };
    uint32_t count = 0;
    zx_handle_t h;
    for (unsigned n = 0; n < fbl::count_of(FSTAB); n++) {
        if (!(FSTAB[n].flags & flags)) {
            continue;
        }
        if ((h = fs_clone(FSTAB[n].name).release()) != ZX_HANDLE_INVALID) {
            nametable[count] = FSTAB[n].mount;
            launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, count++));
        }
    }
    launchpad_set_nametable(lp, count, nametable);

    if (stdiofd < 0) {
        if ((status = zx_debuglog_create(ZX_HANDLE_INVALID, 0, &h) < 0)) {
            launchpad_abort(lp, status, "devmgr: cannot create debuglog handle");
        } else {
            launchpad_add_handle(lp, h, PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO | 0));
        }
    } else {
        launchpad_clone_fd(lp, stdiofd, FDIO_FLAG_USE_FOR_STDIO | 0);
        close(stdiofd);
    }

    launchpad_add_handles(lp, hcount, handles, types);

    zx::process proc;
    const char* errmsg;
    if ((status = launchpad_go(lp, proc.reset_and_get_address(), &errmsg)) < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n",
               argv[0], name, errmsg, status);
    } else {
        if (out_proc != nullptr) {
            *out_proc = std::move(proc);
        }
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
    return status;
}

zx_status_t devmgr_launch_cmdline(
    const char* me, const zx::job& job, const char* name,
    zx_status_t (*load)(void* ctx, launchpad_t*, const char* file), void* ctx,
    const char* cmdline,
    const zx_handle_t* handles, const uint32_t* types, size_t hcount,
    zx::process* proc, uint32_t flags) {

    // Get the full commandline by splitting on '+'.
    char* buf = strdup(cmdline);
    if (buf == nullptr) {
        printf("%s: Can't parse + command: %s\n", me, cmdline);
        return ZX_ERR_UNAVAILABLE;
    }
    const int MAXARGS = 8;
    char* argv[MAXARGS];
    int argc = 0;
    char* token;
    char* rest = buf;
    while (argc < MAXARGS && (token = strtok_r(rest, "+", &rest))) {
        argv[argc++] = token;
    }

    printf("%s: starting", me);
    for (int i = 0; i < argc; i++) {
        printf(" '%s'", argv[i]);
    }
    printf("...\n");

    zx_status_t status = devmgr_launch(
        job, name, load, ctx, argc, (const char* const*)argv, nullptr, -1,
        handles, types, hcount, proc, flags);

    free(buf);

    return status;
}

} // namespace
