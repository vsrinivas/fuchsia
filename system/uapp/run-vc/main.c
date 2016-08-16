// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <limits.h>
#include <magenta/processargs.h>
#include <magenta/types.h>
#include <mxio/util.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, const char* const* argv, const char* const* envp) {
    int fd = open("/dev/class/console/vc", O_RDWR);
    if (fd < 0) {
        printf("Error %d opening a new vc\n", fd);
        return fd;
    }

    // start mxsh if no arguments
    char pname[128];
    int pargc;
    bool mxsh;
    const char* pargv[1] = { "/boot/bin/mxsh" };
    if ((mxsh = argc == 1)) {
        strcpy(pname, "mxsh:vc");
        pargc = 1;
    } else {
        char* bname = strrchr(argv[1], '/');
        snprintf(pname, sizeof(pname), "%s:vc", bname ? bname + 1 : argv[1]);
        pargc = argc - 1;
    }

    launchpad_t* lp;
    mx_status_t status = launchpad_create(pname, &lp);
    if (status != NO_ERROR) {
        printf("Error %d in launchpad_create\n", status);
        return status;
    }

    status = launchpad_clone_mxio_root(lp);
    if (status != NO_ERROR) {
        printf("Error %d in launchpad_clone_mxio_root\n", status);
        return status;
    }

    mx_handle_t handles[MXIO_MAX_HANDLES];
    uint32_t types[MXIO_MAX_HANDLES];
    mx_status_t n = mxio_clone_fd(fd, fd, handles, types);
    if (n < 0) {
        printf("Error %d in mxio_clone_fd\n", n);
        return n;
    }
    for (int i = 0; i < n; ++i) {
        types[i] |= MX_HND_INFO(
            MX_HND_INFO_TYPE(types[i]),
            MX_HND_INFO_ARG(types[i]) | MXIO_FLAG_USE_FOR_STDIO);
    }

    status = launchpad_add_handles(lp, n, handles, types);
    if (status != NO_ERROR) {
        printf("Error %d in launchpad_add_handles\n", status);
        return status;
    }

    printf("starting process %s\n", pargv[0]);

    status = launchpad_arguments(lp, pargc, mxsh ? pargv : &argv[1]);
    if (status != NO_ERROR) {
        printf("Error %d in launchpad_arguments\n", status);
        return status;
    }

    status = launchpad_environ(lp, envp);
    if (status != NO_ERROR) {
        printf("Error %d in launchpad_environ\n", status);
        return status;
    }

    status = launchpad_elf_load(
        lp, launchpad_vmo_from_file(mxsh ? pargv[0] : argv[1]));
    if (status != NO_ERROR) {
        printf("Error %d in launchpad_elf_load\n", status);
        return status;
    }

    mx_handle_t proc = launchpad_start(lp);
    if (proc < 0) {
        printf("Error %d in launchpad_start\n", proc);
        return proc;
    }
    launchpad_destroy(lp);

    close(fd);
    return 0;
}
