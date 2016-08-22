// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"

#include <fcntl.h>
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

struct callback_data {
    uint8_t* bootfs;
    unsigned int file_count;
};

static void callback(void* arg, const char* path, size_t off, size_t len) {
    struct callback_data* cd = arg;
    //printf("bootfs: %s @%zd (%zd bytes)\n", path, off, len);
    bootfs_add_file(path, cd->bootfs + off, len);
    ++cd->file_count;
}

static const char* env[] = {
#if !defined(__x86_64__)
    // make debugging less painful
    "LD_DEBUG=1",
#endif
    NULL,
};

void devmgr_launch(const char* name, const char* app, const char* arg, const char* device) {
    mx_handle_t hnd[1 + 5 * VFS_MAX_HANDLES];
    uint32_t ids[1 + 5 * VFS_MAX_HANDLES];
    unsigned n = 1;
    mx_status_t r;
    const char* args[2] = { app, arg };

    ids[0] = MX_HND_TYPE_MXIO_ROOT;
    hnd[0] = vfs_create_root_handle();

    hnd[n] = launchpad_get_vdso_vmo();
    if (hnd[n] > 0)
        ids[n++] = MX_HND_INFO(MX_HND_TYPE_VDSO_VMO, 0);
    else
        printf("devmgr: launchpad_get_vdso_vmo failed (%d)\n", hnd[n]);

    if (device == NULL) {
        // start with log handles, no stdin
        device = "debuglog";
        ids[n] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 1);
        if ((hnd[n] = mx_log_create(0)) < 0) {
            goto fail;
        }
        n++;
    } else {
        int fd;
        if ((fd = open(device, O_RDWR)) < 0) {
            goto fail;
        }
        r = mxio_clone_fd(fd, MXIO_FLAG_USE_FOR_STDIO | 0, hnd + n, ids + n);
        close(fd);
        if (r < 0) {
            goto fail;
        }
        n += r;
    }
    printf("devmgr: launch %s on %s\n", app, device);
    mx_handle_t proc = launchpad_launch(name, arg ? 2 : 1, args, env, n, hnd, ids);
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
    mx_handle_t proc = launchpad_launch(name, 3, args, (const char* const*)environ, 2, hnd, ids);
    if (proc < 0)
        printf("devmgr: launch failed: %d\n", proc);
    else
        mx_handle_close(proc);
}

static unsigned int setup_bootfs_vmo(unsigned int n, mx_handle_t vmo) {
    uint64_t size;
    mx_status_t status = mx_vm_object_get_size(vmo, &size);
    if (status != NO_ERROR) {
        cprintf("devmgr: failed to get bootfs #%u size (%d)\n", n, status);
        return 0;
    }
    if (size == 0)
        return 0;
    mx_vaddr_t addr;
    status = mx_process_vm_map(0, vmo, 0, size, &addr, MX_VM_FLAG_PERM_READ);
    if (status != NO_ERROR) {
        cprintf("devmgr: failed to map bootfs #%u (%d)\n", n, status);
        return 0;
    }
    struct callback_data cd = {
        .bootfs = (void*)addr,
    };
    bootfs_parse(cd.bootfs, size, &callback, &cd);
    return cd.file_count;
}

static void setup_bootfs(void) {
    mx_handle_t vmo;
    for (unsigned int n = 0;
         (vmo = mxio_get_startup_handle(
             MX_HND_INFO(MX_HND_TYPE_BOOTFS_VMO, n))) != MX_HANDLE_INVALID;
        ++n) {
        unsigned int count = setup_bootfs_vmo(n, vmo);
        mx_handle_close(vmo);
        if (count > 0)
            printf("devmgr: bootfs #%u contains %u file%s\n",
                   n, count, (count == 1) ? "" : "s");
    }
}

void devmgr_vfs_init(void) {
    printf("devmgr: vfs init\n");

    setup_bootfs();

    vfs_init(vfs_get_root());

    // give our own process access to files in the vfs
    mx_handle_t h = vfs_create_root_handle();
    if (h > 0) {
        mxio_install_root(mxio_remote_create(h, 0));
    }
}
