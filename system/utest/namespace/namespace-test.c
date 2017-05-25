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

typedef struct {
    const char* local;
    const char* remote;
} nstab_t;

static nstab_t NS[] = {
    { "/bin", "/boot/bin" },
    { "/lib", "/boot/lib" },
    { "/fake", "/boot" },
    { "/fake/dev", "/dev" },
    { "/fake/tmp", "/tmp" },
    { "/fake/dev/class/pci/xyz", "/boot/src" },
};

static void listdir(const char* path) {
    printf("--- listdir '%s' ---\n", path);
    DIR* dir = opendir(path);
    if (dir == NULL) {
        return;
    }
    struct dirent* de;
    while ((de = readdir(dir)) != NULL) {
        printf("%-16s type=%d\n", de->d_name, de->d_type);
    }
    closedir(dir);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        return run_in_namespace("/boot/bin/sh", argc - 1, argv + 1);
    }
    mxio_ns_t* ns;
    mx_status_t r;
    if ((r = mxio_ns_create(&ns)) < 0) {
        printf("ns create %d\n", r);
        return -1;
    }
    for (unsigned n = 0; n < sizeof(NS) / sizeof(NS[0]); n++) {
        int fd = open(NS[n].remote, O_RDONLY | O_DIRECTORY);
        if (fd < 0) {
            printf("ns open '%s' failed\n", NS[n].remote);
            return -1;
        }
        if ((r = mxio_ns_bind_fd(ns, NS[n].local, fd)) < 0) {
            printf("ns bind '%s' failed: %d\n", NS[n].local, r);
            return -1;
        }
        close(fd);
    }
    if ((r = mxio_ns_chdir(ns)) < 0) {
        printf("ns chdir failed: %d\n", r);
        return -1;
    }

    // should show "bin", "lib", "fake" -- our rootdir
    listdir(".");

    // should show "dev" (local), "bin", "data", "lib", etc (/boot)
    listdir("fake");

    // should show "class" (local), "misc", "pci", "null", etc (/dev)
    listdir("fake/dev");

    // should show classes, with "pci" being local
    listdir("fake/dev/class");

    // should show numeric pci devices, with "xyz" local
    listdir("fake/dev/class/pci");

    // should show contents of /boot/src
    listdir("fake/dev/class/pci/xyz");
    return 0;
}
