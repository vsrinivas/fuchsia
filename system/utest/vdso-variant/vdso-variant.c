// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <magenta/syscalls.h>
#include <magenta/status.h>
#include <mxio/io.h>
#include <stdio.h>
#include <unistd.h>

#define VDSO_FILE "/boot/kernel/vdso/test1"

int main(void) {
    int fd = open(VDSO_FILE, O_RDONLY);
    if (fd < 0) {
        printf("%s: %m\n", VDSO_FILE);
        return 1;
    }

    mx_handle_t vdso_vmo;
    mx_status_t status = mxio_get_exact_vmo(fd, &vdso_vmo);
    close(fd);
    if (status != MX_OK) {
        printf("mxio_get_exact_vmo(%d): %s\n", fd, mx_status_get_string(status));
        return status;
    }

    launchpad_set_vdso_vmo(vdso_vmo);

    launchpad_t* lp;
    launchpad_create(MX_HANDLE_INVALID, "vdso-variant-helper", &lp);
    launchpad_clone(lp, LP_CLONE_ALL);
    launchpad_set_args(lp, 1, (const char*[]){"vdso-variant-helper"});
    launchpad_load_from_file(lp, "/boot/bin/vdso-variant-helper");
    mx_handle_t proc;
    const char* errmsg;
    status = launchpad_go(lp, &proc, &errmsg);
    if (status != MX_OK) {
        printf("launchpad_go: %s\n", errmsg);
        return status;
    }

    status = mx_object_wait_one(proc, MX_PROCESS_TERMINATED,
                                MX_TIME_INFINITE, NULL);
    if (status != MX_OK) {
        printf("mx_object_wait_one: %s\n", mx_status_get_string(status));
        return status;
    }
    mx_info_process_t info;
    status = mx_object_get_info(proc, MX_INFO_PROCESS, &info, sizeof(info),
                                NULL, NULL);
    if (status != MX_OK) {
        printf("mx_object_get_info: %s\n", mx_status_get_string(status));
        return status;
    }

    return info.return_code;
}
