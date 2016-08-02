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

#include <launchpad/launchpad.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <runtime/tls.h>

#include "vfs.h"

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
    bootfs_add_file(path, bootfs + off, len);
    bootfs_end = off + ((len + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)));
    bootfiles_count++;
}

void devmgr_launch(const char* name, const char* app, const char* arg, const char* device) {
    mx_handle_t hnd[5 * VFS_MAX_HANDLES];
    uint32_t ids[5 * VFS_MAX_HANDLES];
    unsigned n = 1;
    mx_status_t r;
    const char* args[2] = { app, arg };

    ids[0] = MX_HND_TYPE_MXIO_ROOT;
    hnd[0] = vfs_create_root_handle();

    if (device == NULL) {
        // start with log handles, no stdin
        device = "debuglog";
        ids[n] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 1);
        if ((hnd[n] = mx_log_create(0)) < 0) {
            goto fail;
        }
        n++;
    } else {
        // TODO: correct open flags once we have them
        if ((r = vfs_open_handles(hnd + n, ids + n, MXIO_FLAG_USE_FOR_STDIO | 0, device, 0)) < 0) {
            goto fail;
        }
        n += r;
    }
    printf("devmgr: launch %s on %s\n", app, device);
    mx_handle_t proc = launchpad_launch(name, arg ? 2 : 1, args, NULL,
                                        n, hnd, ids);
    if (proc < 0)
        printf("devmgr: launchpad_launch failed: %d\n", proc);
    else
        mx_handle_close(proc);
    return;
fail:
    while (n > 0) {
        n--;
        mx_handle_close(hnd[n]);
    }
}

void devmgr_launch_devhost(const char* name, mx_handle_t h,
                           const char* arg0, const char* arg1) {
    const char* binname = "/boot/bin/devmgr";

    // if absolute path provided, this is a dedicated driver
    if (name[0] == '/') {
        binname = name;
    }

    const char* args[3] = {
        binname, arg0, arg1,
    };

    mx_handle_t hnd[2];
    uint32_t ids[2];
    ids[0] = MX_HND_TYPE_MXIO_ROOT;
    hnd[0] = vfs_create_root_handle();
    ids[1] = MX_HND_TYPE_USER1;
    hnd[1] = h;
    printf("devmgr: launch: %s %s %s\n", name, arg0, arg1);
    mx_handle_t proc = launchpad_launch(name, 3, args, NULL, 2, hnd, ids);
    if (proc < 0)
        printf("devmgr: launch failed: %d\n", proc);
    else
        mx_handle_close(proc);
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

    vfs_init(vfs_get_root());

    // give our own process access to files in the vfs
    mx_handle_t h = vfs_create_root_handle();
    if (h > 0) {
        mxio_install_root(mxio_remote_create(h, 0));
    }
}
