// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>

#include <bootdata/decompress.h>

#include <magenta/bootdata.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
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
        NULL, // placeholder for LDSO_TRACE_ENV
        NULL
    };

    if (getenv(LDSO_TRACE_CMDLINE)) {
        unsigned int i;
        for (i = 0; i < countof(env) - 1; ++i) {
            if (env[i] == NULL)
                break;
        }
        env[i] = LDSO_TRACE_ENV;
    }

    mx_handle_t job_copy = MX_HANDLE_INVALID;;
    mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_copy);

    launchpad_t* lp;
    launchpad_create(job_copy, name, &lp);
    launchpad_load_from_file(lp, argv[0]);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, env);

    mx_handle_t h = vfs_create_global_root_handle();
    launchpad_add_handle(lp, h, MX_HND_TYPE_MXIO_ROOT);

    if (stdiofd < 0) {
        mx_status_t r;
        if ((r = mx_log_create(0, &h) < 0)) {
            launchpad_abort(lp, r, "devmgr: cannot create debuglog handle");
        } else {
            launchpad_add_handle(lp, h, MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 0));
        }
    } else {
        launchpad_clone_fd(lp, stdiofd, MXIO_FLAG_USE_FOR_STDIO | 0);
        close(stdiofd);
    }

    launchpad_add_handles(lp, hcount, handles, types);

    const char* errmsg;
    mx_status_t status = launchpad_go(lp, NULL, &errmsg);
    if (status < 0) {
        printf("devmgr: launchpad %s (%s) failed: %s: %d\n",
               argv[0], name, errmsg, status);
    } else {
        printf("devmgr: launch %s (%s) OK\n", argv[0], name);
    }
}

static void start_system_init(void) {
    thrd_t t;
    int r = thrd_create_with_name(&t, devmgr_start_system_init, NULL, "system-init");
    if (r == thrd_success) {
        thrd_detach(t);
    }
}

static bool has_secondary_bootfs = false;
static ssize_t setup_bootfs_vmo(unsigned n, mx_handle_t vmo) {
    uint64_t size;
    mx_status_t status = mx_vmo_get_size(vmo, &size);
    if (status != NO_ERROR) {
        printf("devmgr: failed to get bootfs #%u size (%d)\n", n, status);
        return status;
    }
    if (size == 0) {
        return 0;
    }
    struct callback_data cd = {
        .vmo = vmo,
        .add_file = (n > 0) ? systemfs_add_file : bootfs_add_file,
    };
    if ((n > 0) && !has_secondary_bootfs) {
        has_secondary_bootfs = true;
        memfs_mount(vfs_create_global_root(), systemfs_get_root());
    }
    bootfs_parse(vmo, size, &callback, &cd);
    printf("devmgr: bootfs #%u contains %u file%s\n",
           n, cd.file_count, (cd.file_count == 1) ? "" : "s");
    return cd.file_count;
}

#define HND_BOOTFS(n) MX_HND_INFO(MX_HND_TYPE_BOOTFS_VMO, n)
#define HND_BOOTDATA(n) MX_HND_INFO(MX_HND_TYPE_BOOTDATA_VMO, n)

static void setup_bootfs(void) {
    mx_handle_t vmo;
    unsigned idx = 0;
    bool skip = true;

    if ((vmo = mx_get_startup_handle(HND_BOOTFS(0)))) {
        setup_bootfs_vmo(idx++, vmo);
    } else {
        printf("devmgr: missing primary bootfs?!\n");
        skip = false;
    }

    for (unsigned n = 0; (vmo = mx_get_startup_handle(HND_BOOTDATA(n))); n++) {
        size_t off = 0;
        for (;;) {
            bootdata_t bootdata;
            size_t actual;
            mx_status_t status = mx_vmo_read(vmo, &bootdata, off, sizeof(bootdata), &actual);
            if ((status < 0) || (actual != sizeof(bootdata))) {
                break;
            }
            if (bootdata.magic != BOOTDATA_MAGIC) {
                break;
            }
            if (bootdata.type == BOOTDATA_TYPE_BOOTFS) {
                if (skip) {
                    skip = false;
                } else {
                    const char* errmsg;
                    mx_handle_t bootfs_vmo;
                    printf("devmgr: decompressing bootfs #%u\n", idx);
                    status = decompress_bootdata(mx_vmar_root_self(), vmo,
                                                 0, bootdata.insize + sizeof(bootdata),
                                                 &bootfs_vmo, &errmsg);
                    if (status < 0) {
                        printf("devmgr: failed to decompress bootdata\n");
                    } else {
                        setup_bootfs_vmo(idx++, bootfs_vmo);

                    }
                }
            } else {
                printf("devmgr: ignoring bootdata type=%08x size=%u\n",
                       bootdata.type, bootdata.insize);
            }
            off += sizeof(bootdata) + bootdata.insize;
        }
        mx_handle_close(vmo);
    }
}

ssize_t devmgr_add_systemfs_vmo(mx_handle_t vmo) {
    ssize_t added = setup_bootfs_vmo(100, vmo);
    if (added > 0) {
        start_system_init();
    }
    return added;
}

bool secondary_bootfs_ready(void) {
    return has_secondary_bootfs;
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
    vfs_uninstall_all(MX_SEC(5));
}
