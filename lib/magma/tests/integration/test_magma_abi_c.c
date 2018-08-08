// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma.h"
#include "test_magma_abi.h"
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h> // for close

bool test_magma_abi_from_c(void)
{
    bool result = true;

    int fd = open("/dev/class/gpu/000", O_RDONLY);
    if (fd < 0) {
        printf("%s:%d open returned %d\n", __FILE__, __LINE__, fd);
        result = false;
    }

    uint64_t device_id = 0;
    magma_status_t status = magma_query(fd, MAGMA_QUERY_DEVICE_ID, &device_id);
    if (status != MAGMA_STATUS_OK) {
        printf("%s:%d magma_query return %d\n", __FILE__, __LINE__, status);
        result = false;
    }

    if (device_id == 0) {
        printf("%s:%d device_id is 0\n", __FILE__, __LINE__);
        result = false;
    }

    struct magma_connection_t* connection = magma_create_connection(fd, MAGMA_CAPABILITY_RENDERING);
    if (!connection) {
        printf("%s:%d magma_open returned null\n", __FILE__, __LINE__);
        result = false;
    }

    magma_release_connection(connection);
    close(fd);

    return result;
}
