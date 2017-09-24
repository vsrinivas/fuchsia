// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <fdio/io.h>
#include <fdio/util.h>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void devmgr_io_init(void) {
    // setup stdout
    zx_handle_t h;
    if (zx_log_create(0, &h) < 0) {
        return;
    }
    fdio_t* logger;
    if ((logger = fdio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    fdio_bind_to_fd(logger, 1, 0);
}

#define USER_MAX_HANDLES 4
#define MAX_ENVP 16
#define CHILD_JOB_RIGHTS (ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER | ZX_RIGHT_READ | ZX_RIGHT_WRITE)

zx_status_t devmgr_launch(zx_handle_t job, const char* name,
                          int argc, const char* const* argv,
                          const char** _envp, int stdiofd,
                          zx_handle_t* handles, uint32_t* types, size_t hcount,
                          zx_handle_t* proc) {

    const char* envp[MAX_ENVP + 1];
    unsigned envn = 0;

    if (getenv(LDSO_TRACE_CMDLINE)) {
        envp[envn++] = LDSO_TRACE_ENV;
    }
    while ((_envp && _envp[0]) && (envn < MAX_ENVP)) {
        envp[envn++] = *_envp++;
    }
    envp[envn++] = NULL;

    zx_handle_t job_copy = ZX_HANDLE_INVALID;
    zx_handle_duplicate(job, CHILD_JOB_RIGHTS, &job_copy);

    launchpad_t* lp;
    launchpad_create(job_copy, name, &lp);

    zx_handle_t file_vmo;
    if ((file_vmo = devmgr_load_file(argv[0])) != ZX_HANDLE_INVALID) {
        launchpad_load_from_vmo(lp, file_vmo);
    } else {
        launchpad_load_from_file(lp, argv[0]);
    }
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);

    // fshost builds its own namespace because of chickens and eggs
    // other processes get pre-assembled namespaces
    if (strcmp(name, "fshost")) {
        const char* nametable[3] = { "/" };
        size_t count = 0;

        zx_handle_t h = fs_root_clone();
        launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, count++));

        //TODO: constrain to /svc/debug, or other as appropriate
        if (strcmp(name, "init") && ((h = svc_root_clone()) != ZX_HANDLE_INVALID)) {
            nametable[count] = "/svc";
            launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, count++));
        }
        if ((h = devfs_root_clone()) != ZX_HANDLE_INVALID) {
            nametable[count] = "/dev";
            launchpad_add_handle(lp, h, PA_HND(PA_NS_DIR, count++));
        }
        launchpad_set_nametable(lp, count, nametable);
    }

    if (stdiofd < 0) {
        zx_status_t r;
        zx_handle_t h;
        if ((r = zx_log_create(0, &h) < 0)) {
            launchpad_abort(lp, r, "devmgr: cannot create debuglog handle");
        } else {
            launchpad_add_handle(lp, h, PA_HND(PA_FDIO_LOGGER, FDIO_FLAG_USE_FOR_STDIO | 0));
        }
    } else {
        launchpad_clone_fd(lp, stdiofd, FDIO_FLAG_USE_FOR_STDIO | 0);
        close(stdiofd);
    }

    launchpad_add_handles(lp, hcount, handles, types);

    const char* errmsg;
    zx_status_t status = launchpad_go(lp, proc, &errmsg);
    if (status < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n",
               argv[0], name, errmsg, status);
    } else {
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
    return status;
}

zx_status_t copy_vmo(zx_handle_t src, zx_off_t offset, size_t length, zx_handle_t* out_dest) {
    zx_handle_t dest;
    zx_status_t status = zx_vmo_create(length, 0, &dest);
    if (status != ZX_OK) {
        return status;
    }

    char buffer[PAGE_SIZE];
    zx_off_t src_offset = offset;
    zx_off_t dest_offset = 0;

    while (length > 0) {
        size_t copy = (length > sizeof(buffer) ? sizeof(buffer) : length);
        size_t actual;
        if ((status = zx_vmo_read(src, buffer, src_offset, copy, &actual)) != ZX_OK) {
            goto fail;
        }
        if ((status = zx_vmo_write(dest, buffer, dest_offset, actual, &actual)) != ZX_OK) {
            goto fail;
        }
        src_offset += actual;
        dest_offset += actual;
        length -= actual;
    }

    *out_dest = dest;
    return ZX_OK;

fail:
    zx_handle_close(dest);
    return status;
}
