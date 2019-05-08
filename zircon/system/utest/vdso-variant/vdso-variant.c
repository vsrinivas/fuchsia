// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <launchpad/launchpad.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/status.h>
#include <lib/fdio/io.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VDSO_FILE "/boot/kernel/vdso/test1"

int main(void) {
    int fd = open(VDSO_FILE, O_RDONLY);
    if (fd < 0) {
        printf("%s: %m\n", VDSO_FILE);
        return 1;
    }

    zx_handle_t vdso_vmo_noexec;
    zx_handle_t vdso_vmo;
    zx_status_t status = fdio_get_vmo_exact(fd, &vdso_vmo_noexec);
    close(fd);
    if (status != ZX_OK) {
        printf("fdio_get_vmo_exact(%d): %s\n", fd, zx_status_get_string(status));
        return status;
    }

    status = zx_vmo_replace_as_executable(vdso_vmo_noexec, ZX_HANDLE_INVALID, &vdso_vmo);
    if (status != ZX_OK) {
        printf("zx_vmo_replace_as_executable(%u, ZX_HANDLE_INVALID, *res): %s\n",
               vdso_vmo_noexec, zx_status_get_string(status));
        return status;
    }

    launchpad_set_vdso_vmo(vdso_vmo);

    launchpad_t* lp;
    launchpad_create(ZX_HANDLE_INVALID, "vdso-variant-helper", &lp);
    launchpad_clone(lp, LP_CLONE_ALL);
    launchpad_set_args(lp, 1, (const char*[]){"vdso-variant-helper"});
    char* root_dir = getenv("TEST_ROOT_DIR");
    ZX_ASSERT(root_dir != NULL);
    static const char kHelperPath[] = "/bin/vdso-variant-helper";
    char path[strlen(root_dir) + strlen(kHelperPath) + 1];
    strcpy(path, root_dir);
    strcat(path, kHelperPath);
    launchpad_load_from_file(lp, path);
    zx_handle_t proc;
    const char* errmsg;
    status = launchpad_go(lp, &proc, &errmsg);
    if (status != ZX_OK) {
        printf("launchpad_go: %s\n", errmsg);
        return status;
    }

    status = zx_object_wait_one(proc, ZX_PROCESS_TERMINATED,
                                ZX_TIME_INFINITE, NULL);
    if (status != ZX_OK) {
        printf("zx_object_wait_one: %s\n", zx_status_get_string(status));
        return status;
    }
    zx_info_process_t info;
    status = zx_object_get_info(proc, ZX_INFO_PROCESS, &info, sizeof(info),
                                NULL, NULL);
    if (status != ZX_OK) {
        printf("zx_object_get_info: %s\n", zx_status_get_string(status));
        return status;
    }

    return info.return_code;
}
