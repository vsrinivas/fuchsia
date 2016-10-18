// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include "magma_system.h"
#include "gtest/gtest.h"

TEST(MagmaSystemAbi, Connection)
{
    int fd = open("/dev/class/display/000", O_RDONLY);
    ASSERT_GE(fd, 0);

    EXPECT_NE(magma_system_get_device_id(fd), 0u);

    auto connection = magma_system_open(fd);
    ASSERT_NE(connection, nullptr);

    magma_system_close(connection);

    close(fd);
}
