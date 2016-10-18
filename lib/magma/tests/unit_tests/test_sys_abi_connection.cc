// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include "magma_system.h"
#include "gtest/gtest.h"

TEST(MagmaSystemAbi, DeviceId)
{
    int fd = open("/dev/class/display/000", O_RDONLY);
    ASSERT_GE(fd, 0);

    EXPECT_NE(magma_system_get_device_id(fd), 0u);

    close(fd);
}

TEST(MagmaSystemAbi, Connection)
{
    int fd = open("/dev/class/display/000", O_RDONLY);
    ASSERT_GE(fd, 0);

    auto connection = magma_system_open(fd);
    ASSERT_NE(connection, nullptr);
}

TEST(MagmaSystemAbi, Context)
{
    int fd = open("/dev/class/display/000", O_RDONLY);
    ASSERT_GE(fd, 0);

    auto connection = magma_system_open(fd);
    ASSERT_NE(connection, nullptr);

    uint32_t context_id;

    EXPECT_TRUE(connection.CreateContext(&context_id_0));

    magma_system_create_context(&connection, &context_id);
    EXPECT_EQ(magma_system_get_error(&connection), 0);

    magma_system_destroy_context(&connection, context_id);
    EXPECT_EQ(magma_system_get_error(&connection), 0);

    magma_system_destroy_context(&connection, context_id_1);
    EXPECT_NE(magma_system_get_error(&connection), 0);

    magma_system_close(connection);

    close(fd);
}
