// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This test is intended to be run manually from within biscotti_guest.

#include "magma.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/unistd.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            printf("Check Failed (%s): \"%s\"\n", #x, strerror(errno));                            \
            exit(EXIT_FAILURE);                                                                    \
        }                                                                                          \
    } while (0)

int main(int argc, char* argv[])
{
    static const char* device_path = "/dev/wl0";
    printf("Open Device %s\n", device_path);
    int fd = open(device_path, O_NONBLOCK);
    CHECK(fd != -1);
    printf("Device Opened\n");

    uint64_t device_id = 0;
    printf("Query Device ID 0x%08X\n", MAGMA_QUERY_DEVICE_ID);
    magma_status_t status = magma_query(fd, MAGMA_QUERY_DEVICE_ID, &device_id);
    CHECK(status == MAGMA_STATUS_OK);
    printf("Device ID: 0x%016lX\n", device_id);

    printf("Create Connection\n");
    auto connection = magma_create_connection(fd, 0);
    CHECK(connection != nullptr);
    printf("Connection Created\n");

    printf("Release Connection\n");
    magma_release_connection(connection);
    printf("Connection Released\n");

    return 0;
}
