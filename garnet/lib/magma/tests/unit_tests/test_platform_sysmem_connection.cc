// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_sysmem_connection.h"
#include "gtest/gtest.h"

class TestPlatformSysmemConnection {
public:
    static void TestCreateBuffer()
    {
        auto connection = magma::PlatformSysmemConnection::Create();

        ASSERT_NE(nullptr, connection.get());

        std::unique_ptr<magma::PlatformBuffer> buffer;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->AllocateBuffer(0, 16384, &buffer));
        ASSERT_TRUE(buffer != nullptr);
        EXPECT_LE(16384u, buffer->size());
    }

    static void TestCreate()
    {
        auto connection = magma::PlatformSysmemConnection::Create();

        ASSERT_NE(nullptr, connection.get());

        std::unique_ptr<magma::PlatformBuffer> buffer;
        std::unique_ptr<magma::PlatformSysmemConnection::BufferDescription> description;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->AllocateTexture(0, MAGMA_FORMAT_R8G8B8A8, 128, 64,
                                                               &buffer, &description));
        EXPECT_TRUE(buffer != nullptr);
        ASSERT_TRUE(description != nullptr);
        EXPECT_TRUE(description->planes[0].bytes_per_row >= 128 * 4);
    }
};

TEST(PlatformSysmemConnection, CreateBuffer) { TestPlatformSysmemConnection::TestCreateBuffer(); }

TEST(PlatformSysmemConnection, Create) { TestPlatformSysmemConnection::TestCreate(); }
