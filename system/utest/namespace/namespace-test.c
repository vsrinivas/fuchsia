// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <mxio/namespace.h>

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
    mxio_ns_t* ns;
    mx_status_t r;
    if ((r = mxio_ns_create(&ns)) < 0) {
        printf("ns create %d\n", r);
        return -1;
    }
    for (unsigned n = 0; n < sizeof(NS) / sizeof(NS[0]); n++) {
        int fd = open(NS[n].remote, O_RDWR);
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