// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <limits.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/io.h>
#include <mxio/util.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, const char* const* argv) {
    int fd = open("/dev/class/console/vc", O_RDWR);
    if (fd < 0) {
        printf("Error %d opening a new vc\n", fd);
        return fd;
    }

    // start shell if no arguments
    char pname[128];
    int pargc;
    bool shell;
    const char* pargv[1] = { "/boot/bin/sh" };
    if ((shell = argc == 1)) {
        strcpy(pname, "sh:vc");
        pargc = 1;
    } else {
        char* bname = strrchr(argv[1], '/');
        snprintf(pname, sizeof(pname), "%s:vc", bname ? bname + 1 : argv[1]);
        pargc = argc - 1;
    }

    launchpad_t* lp;
    launchpad_create(0, pname, &lp);
    launchpad_clone(lp, LP_CLONE_MXIO_ROOT | LP_CLONE_ENVIRON);
    launchpad_clone_fd(lp, fd, 0);
    launchpad_clone_fd(lp, fd, 1);
    launchpad_clone_fd(lp, fd, 2);
    launchpad_set_args(lp, pargc, shell ? pargv : &argv[1]);

    // Forward MX_HND_TYPE_APPLICATION_ENVIRONMENT if we have one.
    mx_handle_t application_environment = mx_get_startup_handle(
        MX_HND_INFO(MX_HND_TYPE_APPLICATION_ENVIRONMENT, 0));
    if (application_environment != MX_HANDLE_INVALID) {
        launchpad_add_handle(lp, application_environment,
            MX_HND_INFO(MX_HND_TYPE_APPLICATION_ENVIRONMENT, 0));
    }

    launchpad_load_from_file(lp, shell ? pargv[0] : argv[1]);

    mx_status_t status;
    const char* errmsg;
    if ((status = launchpad_go(lp, NULL, &errmsg)) < 0) {
        fprintf(stderr, "error %d launching: %s\n", status, errmsg);
    }

    close(fd);
    return 0;
}
