// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "devmgr.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <runtime/tls.h>

#include "vfs.h"

mx_status_t vnd_get_node(vnode_t** out, mx_device_t* dev);

vnode_t* vnb_get_root(void);
mx_status_t vnb_add_file(const char* path, void* data, size_t len);
mx_status_t vnb_mount_at(vnode_t* vn, const char* dirname);

vnode_t* mem_get_root(void);

typedef struct bootfile bootfile_t;
struct bootfile {
    bootfile_t* next;
    const char* name;
    void* data;
    size_t len;
};

static uint8_t* bootfs = NULL;
static int bootfiles_count = 0;
static size_t bootfs_end = 0;

static void callback(const char* path, size_t off, size_t len) {
    //printf("bootfs: %s @%zd (%zd bytes)\n", path, off, len);
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "boot/%s", path);
    vnb_add_file(tmp, bootfs + off, len);
    bootfs_end = off + ((len + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)));
    bootfiles_count++;
}

void devmgr_launch(const char* name, const char* app, const char* device) {
    mx_handle_t hnd[5 * VFS_MAX_HANDLES];
    uint32_t ids[5 * VFS_MAX_HANDLES];
    unsigned n = 1;
    mx_status_t r;

    ids[0] = MX_HND_TYPE_MXIO_ROOT;
    hnd[0] = vfs_create_root_handle();

    // TODO: correct open flags once we have them
    if ((r = vfs_open_handles(hnd + n, ids + n, 0, device, 0)) < 0) {
        goto fail;
    }
    n += r;
    if ((r = vfs_open_handles(hnd + n, ids + n, 1, device, 0)) < 0) {
        goto fail;
    }
    n += r;
    if ((r = vfs_open_handles(hnd + n, ids + n, 2, device, 0)) < 0) {
        goto fail;
    }
    n += r;
    printf("devmgr: launch shell on %s\n", device);
    mxio_start_process_etc(name, 1, (char**)&app, n, hnd, ids);
    return;
fail:
    while (n > 0) {
        n--;
        _magenta_handle_close(hnd[n]);
    }
}

void devmgr_launch_devhost(const char* name, mx_handle_t h,
                           const char* arg0, const char* arg1) {
    const char* binname = "/boot/bin/devmgr";
    const char* args[3] = {
        binname, arg0, arg1,
    };
    mx_handle_t hnd[2];
    uint32_t ids[2];
    ids[0] = MX_HND_TYPE_MXIO_ROOT;
    hnd[0] = vfs_create_root_handle();
    ids[1] = MX_HND_TYPE_USER1;
    hnd[1] = h;
    printf("devmgr: launch host: %s %s\n", arg0, arg1);
    mxio_start_process_etc(name, 3, (char**)args, 2, hnd, ids);
}

void devmgr_io_init(void) {
    // setup stdout
    uint32_t flags = devmgr_is_remote ? MX_LOG_FLAG_DEVICE : MX_LOG_FLAG_DEVMGR;
    mx_handle_t h;
    if ((h = _magenta_log_create(flags)) < 0) {
        return;
    }
    mxio_t* logger;
    if ((logger = mxio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(logger, 1);
}

void devmgr_vfs_init(void* _bootfs, size_t len) {
    printf("devmgr: vfs init\n");

    // setup bootfs if present
    if (_bootfs != NULL) {
        bootfs = _bootfs;
        bootfs_parse(bootfs, len, callback);

        // there can be a second bootfs (if passed as initrd)
        if (bootfs_end < len) {
            bootfs = (void*) ((char*) bootfs + bootfs_end);
            bootfs_parse(bootfs, len - bootfs_end, callback);
        }

        if (bootfiles_count) {
            printf("devmgr: bootfs contains %d file%s\n",
                   bootfiles_count, (bootfiles_count == 1) ? "" : "s");
        }
    }

    // init vfs, bootfs is root
    vfs_init(vnb_get_root());

    // install devfs at /dev
    vnode_t* vn;
    if (vnd_get_node(&vn, devmgr_device_root()) == NO_ERROR) {
        vnb_mount_at(vn, "dev");
    }

    // install memfs at /tmp
    vnb_mount_at(mem_get_root(), "tmp");

    // give our own process access to files in the vfs
    mx_handle_t h = vfs_create_root_handle();
    if (h > 0) {
        mxio_install_root(mxio_remote_create(h, 0));
    }
}
