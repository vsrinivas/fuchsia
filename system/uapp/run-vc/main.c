// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <limits.h>
#include <magenta/device/console.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxio/io.h>
#include <mxio/util.h>
#include <mxio/watcher.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_argc;
static const char* const* g_argv;

static mx_status_t console_device_added(int dirfd, int event, const char* name, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return NO_ERROR;
    }

    if (strcmp(name, "vc")) {
        return NO_ERROR;
    }

    int fd = openat(dirfd, name, O_RDWR);
    if (fd < 0) {
        printf("Error %d opening a new vc\n", fd);
        return 1;
    }

    ioctl_console_set_active_vc(fd);

    // start shell if no arguments
    char pname[128];
    int pargc;
    bool shell;
    const char* pargv[1] = { "/boot/bin/sh" };
    if ((shell = g_argc == 1)) {
        strcpy(pname, "sh:vc");
        pargc = 1;
    } else {
        char* bname = strrchr(g_argv[1], '/');
        snprintf(pname, sizeof(pname), "%s:vc", bname ? bname + 1 : g_argv[1]);
        pargc = g_argc - 1;
    }

    launchpad_t* lp;
    launchpad_create(0, pname, &lp);
    launchpad_clone(lp, LP_CLONE_MXIO_ROOT | LP_CLONE_ENVIRON);
    launchpad_clone_fd(lp, fd, 0);
    launchpad_clone_fd(lp, fd, 1);
    launchpad_clone_fd(lp, fd, 2);
    launchpad_set_args(lp, pargc, shell ? pargv : &g_argv[1]);
    launchpad_load_from_file(lp, shell ? pargv[0] : g_argv[1]);

    mx_status_t status;
    const char* errmsg;
    if ((status = launchpad_go(lp, NULL, &errmsg)) < 0) {
        fprintf(stderr, "error %d launching: %s\n", status, errmsg);
    }

    close(fd);

    // stop polling
    return 1;
}

int main(int argc, const char* const* argv) {
    g_argc = argc;
    g_argv = argv;

    int dirfd;
    if ((dirfd = open("/dev/class/console", O_DIRECTORY|O_RDONLY)) >= 0) {
        mxio_watch_directory(dirfd, console_device_added, NULL);
    }
    close(dirfd);
    return 0;
}
