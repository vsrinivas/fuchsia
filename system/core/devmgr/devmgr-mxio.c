// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct bootfile bootfile_t;
struct bootfile {
    bootfile_t* next;
    const char* name;
    void* data;
    size_t len;
};

struct callback_data {
    mx_handle_t vmo;
    unsigned int file_count;
    mx_status_t (*add_file)(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);
};

static void callback(void* arg, const char* path, size_t off, size_t len) {
    struct callback_data* cd = arg;
    //printf("bootfs: %s @%zd (%zd bytes)\n", path, off, len);
    cd->add_file(path, cd->vmo, off, len);
    ++cd->file_count;
}

#define USER_MAX_HANDLES 4

void devmgr_launch(mx_handle_t job,
                   const char* name, int argc, const char** argv,
                   const char* extra_env, int stdiofd,
                   mx_handle_t* handles, uint32_t* types, size_t hcount) {
    const char* env[] = {
#if !(defined(__x86_64__) || defined(__aarch64__))
        // make debugging less painful
        "LD_DEBUG=1",
#endif
        extra_env,
        NULL
    };

    printf("devmgr: launch %s (%s)\n", argv[0], name);

    mx_handle_t job_copy;
    mx_status_t r;
    if ((r = mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_copy)) < 0) {
        goto done_no_lp;
    }

    launchpad_t* lp;
    if ((r = launchpad_create(job_copy, name, &lp)) < 0) {
        goto done_no_lp;
    }

    // TODO: launchpad_load_file(lp, path) & launchpad_load_vmo(lp, vmo)
    if ((r = launchpad_elf_load(lp, launchpad_vmo_from_file(argv[0]))) < 0) {
        goto done;
    }
    if ((r = launchpad_load_vdso(lp, MX_HANDLE_INVALID)) < 0) {
        goto done;
    }
    if ((r = launchpad_add_vdso_vmo(lp)) < 0) {
        goto done;
    }

    if ((r = launchpad_arguments(lp, argc, argv)) < 0) {
        goto done;
    }
    if ((r = launchpad_environ(lp, env)) < 0) {
        goto done;
    }

    mx_handle_t h = vfs_create_global_root_handle();
    if ((r = launchpad_add_handle(lp, h, MX_HND_TYPE_MXIO_ROOT)) < 0) {
        mx_handle_close(h);
        goto done;
    }

    if (stdiofd < 0) {
        if ((r = mx_log_create(0, &h) < 0)) {
            goto done;
        }
        if ((r = launchpad_add_handle(lp, h, MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 0))) < 0) {
            goto done;
        }
    } else {
        r = launchpad_clone_fd(lp, stdiofd, MXIO_FLAG_USE_FOR_STDIO | 0);
        close(stdiofd);
        stdiofd = -1;
        if (r < 0) {
            goto done;
        }
    }
    if ((r = launchpad_add_handles(lp, hcount, handles, types)) < 0) {
        goto done;
    }
    hcount = 0;

    if ((h = launchpad_start(lp)) < 0) {
        printf("devmgr: launchpad_start() %s (%s) failed: %d\n", argv[0], name, h);
        r = 0;
    } else {
        mx_handle_close(h);
    }

done:
    launchpad_destroy(lp);

done_no_lp:
    while (hcount > 0) {
        mx_handle_close(handles[--hcount]);
    }
    if (stdiofd >= 0) {
        close(stdiofd);
    }
    if (r < 0) {
        printf("devmgr: launch %s (%s): failed: %d\n", argv[0], name, r);
    }
}

static ssize_t setup_bootfs_vmo(unsigned int n, mx_handle_t vmo) {
    uint64_t size;
    mx_status_t status = mx_vmo_get_size(vmo, &size);
    if (status != NO_ERROR) {
        printf("devmgr: failed to get bootfs #%u size (%d)\n", n, status);
        return status;
    }
    if (size == 0)
        return 0;
    struct callback_data cd = {
        .vmo = vmo,
        .add_file = (n > 0) ? systemfs_add_file : bootfs_add_file,
    };
    bootfs_parse(vmo, size, &callback, &cd);
    return cd.file_count;
}

static void setup_bootfs(void) {
    mx_handle_t vmo;
    for (unsigned int n = 0;
         (vmo = mxio_get_startup_handle(
             MX_HND_INFO(MX_HND_TYPE_BOOTFS_VMO, n))) != MX_HANDLE_INVALID;
        ++n) {
        ssize_t count = setup_bootfs_vmo(n, vmo);
        if (count > 0)
            printf("devmgr: bootfs #%u contains %zd file%s\n",
                   n, count, (count == 1) ? "" : "s");
    }
}

ssize_t devmgr_add_systemfs_vmo(mx_handle_t vmo) {
    return setup_bootfs_vmo(100, vmo);
}

void devmgr_vfs_init(void) {
    printf("devmgr: vfs init\n");

    setup_bootfs();

    vfs_global_init(vfs_create_global_root());

    // give our own process access to files in the vfs
    mx_handle_t h = vfs_create_global_root_handle();
    if (h > 0) {
        mxio_install_root(mxio_remote_create(h, 0));
    }
}

void devmgr_vfs_exit(void) {
    vfs_uninstall_all();
}
