// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <mxio/namespace.h>
#include <magenta/syscalls.h>

int run_in_namespace(const char* bin, size_t count, char** mapping) {
    mxio_ns_t* ns;
    mx_status_t r;
    if ((r = mxio_ns_create(&ns)) < 0) {
        fprintf(stderr, "failed to create namespace: %d\n", r);
        return -1;
    }
    for (size_t n = 0; n < count; n++) {
        char* dst = *mapping++;
        char* src = strchr(dst, '=');
        if (src == NULL) {
            fprintf(stderr, "error: mapping '%s' not in form of '<dst>=<src>'\n", dst);
            return -1;
        }
        *src++ = 0;
        int fd = open(src, O_RDONLY | O_DIRECTORY);
        if (fd < 0) {
            fprintf(stderr, "error: cannot open '%s'\n", src);
            return -1;
        }
        if ((r = mxio_ns_bind_fd(ns, dst, fd)) < 0) {
            fprintf(stderr, "error: binding fd %d to '%s' failed: %d\n", fd, dst, r);
            close(fd);
            return -1;
        }
        close(fd);
    }
    mxio_flat_namespace_t* flat;
    mxio_ns_opendir(ns);
    r = mxio_ns_export(ns, &flat);
    mxio_ns_destroy(ns);
    if (r < 0) {
        fprintf(stderr, "error: cannot flatten namespace: %d\n", r);
        return -1;
    }

    for (size_t n = 0; n < flat->count; n++) {
        fprintf(stderr, "{ .handle = 0x%08x, type = 0x%08x, .path = '%s' },\n",
                flat->handle[n], flat->type[n], flat->path[n]);
    }

    launchpad_t* lp;
    launchpad_create(0, bin, &lp);
    launchpad_clone(lp, LP_CLONE_MXIO_STDIO | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);
    launchpad_set_args(lp, 1, &bin);
    launchpad_set_nametable(lp, flat->count, flat->path);
    launchpad_add_handles(lp, flat->count, flat->handle, flat->type);
    launchpad_load_from_file(lp, bin);
    free(flat);
    const char* errmsg;
    mx_handle_t proc;
    if ((r = launchpad_go(lp, &proc, &errmsg)) < 0) {
        fprintf(stderr, "error: failed to launch shell: %s\n", errmsg);
        return -1;
    }
    mx_object_wait_one(proc, MX_PROCESS_TERMINATED, MX_TIME_INFINITE, NULL);
    fprintf(stderr, "[done]\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        return run_in_namespace("/boot/bin/sh", argc - 1, argv + 1);
    }

    printf("Usage: %s [dst=src]+, to run a shell with src mapped to dst\n", argv[0]);
    return -1;
}
