// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/syscalls.h>
#include <unittest/unittest.h>

#include "filesystems.h"

bool test_vmo_create(void) {
    BEGIN_TEST;

    if (!test_info->supports_create_by_vmo) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0755), 0);
    int dirfd = open("::dir", O_DIRECTORY | O_RDONLY);
    ASSERT_GT(dirfd, 0);

    size_t vmosize = PAGE_SIZE;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(vmosize, 0, &vmo), ZX_OK);

    char buf[1024];
    vmo_create_config_t* config = reinterpret_cast<vmo_create_config_t*>(buf);
    config->vmo = vmo;
    strcpy(config->name, "vmofile");

    size_t config_size = sizeof(vmo_create_config_t) + strlen("vmofile") + 1;
    ASSERT_EQ(ioctl_vfs_vmo_create(dirfd, config, config_size), ZX_OK);
    int fd = open("::dir/vmofile", O_RDWR);
    ASSERT_GT(fd, 0);
    ASSERT_EQ(close(fd), 0);
    ASSERT_EQ(unlink("::dir/vmofile"), 0);
    ASSERT_EQ(close(dirfd), 0);
    ASSERT_EQ(rmdir("::dir"), 0);

    END_TEST;
}

bool test_vmo_resizable_create(void) {
    BEGIN_TEST;

    if (!test_info->supports_create_by_vmo) {
        return true;
    }

    ASSERT_EQ(mkdir("::dir", 0755), 0);
    int dirfd = open("::dir", O_DIRECTORY | O_RDONLY);
    ASSERT_GT(dirfd, 0);

    size_t vmosize = PAGE_SIZE;
    zx_handle_t vmo;
    ASSERT_EQ(zx_vmo_create(vmosize, 0, &vmo), ZX_OK);
    zx_handle_t backup_handle;
    ASSERT_EQ(zx_handle_duplicate(vmo, ZX_RIGHT_SAME_RIGHTS, &backup_handle), ZX_OK);

    char buf[1024];
    vmo_create_config_t* config = reinterpret_cast<vmo_create_config_t*>(buf);
    config->vmo = vmo;
    strcpy(config->name, "vmofile");

    size_t config_size = sizeof(vmo_create_config_t) + strlen("vmofile") + 1;
    // When we have both "vmo" and the "backup_handle" open, the call will fail.
    ASSERT_EQ(ioctl_vfs_vmo_create(dirfd, config, config_size), ZX_ERR_INVALID_ARGS);

    // vmo_create always consumes the incoming handle;
    // now "backup_handle" is the ONLY handle to the vmo left open.
    config->vmo = backup_handle;
    ASSERT_EQ(ioctl_vfs_vmo_create(dirfd, config, config_size), ZX_OK);

    ASSERT_EQ(unlink("::dir/vmofile"), 0);
    ASSERT_EQ(close(dirfd), 0);
    ASSERT_EQ(rmdir("::dir"), 0);

    END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(fs_vmo_tests,
    RUN_TEST_MEDIUM(test_vmo_create)
    RUN_TEST_MEDIUM(test_vmo_resizable_create)
)
