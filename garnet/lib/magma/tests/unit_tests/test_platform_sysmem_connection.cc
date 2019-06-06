// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform_handle.h"
#include "platform_sysmem_connection.h"
#include "gtest/gtest.h"

class TestPlatformSysmemConnection {
public:
    static void TestCreateBuffer()
    {
        auto connection = magma_sysmem::PlatformSysmemConnection::Create();

        ASSERT_NE(nullptr, connection.get());

        std::unique_ptr<magma::PlatformBuffer> buffer;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->AllocateBuffer(0, 16384, &buffer));
        ASSERT_TRUE(buffer != nullptr);
        EXPECT_LE(16384u, buffer->size());
    }

    static void TestSetConstraints()
    {
        auto connection = magma_sysmem::PlatformSysmemConnection::Create();

        ASSERT_NE(nullptr, connection.get());

        uint32_t token;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->CreateBufferCollectionToken(&token).get());
        std::unique_ptr<magma_sysmem::PlatformBufferCollection> collection;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->ImportBufferCollection(token, &collection).get());

        magma_buffer_format_constraints_t buffer_constraints{};
        buffer_constraints.count = 1;
        buffer_constraints.usage = 0;
        buffer_constraints.secure_permitted = false;
        buffer_constraints.secure_required = false;
        buffer_constraints.cpu_domain_supported = true;
        buffer_constraints.min_size_bytes = 0;

        std::unique_ptr<magma_sysmem::PlatformBufferConstraints> constraints;
        EXPECT_EQ(MAGMA_STATUS_OK,
                  connection->CreateBufferConstraints(&buffer_constraints, &constraints).get());

        // Create a set of basic 512x512 RGBA image constraints.
        magma_image_format_constraints_t image_constraints{};
        image_constraints.image_format = MAGMA_FORMAT_R8G8B8A8;
        image_constraints.has_format_modifier = false;
        image_constraints.format_modifier = 0;
        image_constraints.width = 512;
        image_constraints.height = 512;
        image_constraints.layers = 1;
        image_constraints.bytes_per_row_divisor = 1;
        image_constraints.min_bytes_per_row = 0;

        EXPECT_NE(MAGMA_STATUS_OK,
                  constraints->SetImageFormatConstraints(1, &image_constraints).get());
        EXPECT_EQ(MAGMA_STATUS_OK,
                  constraints->SetImageFormatConstraints(0, &image_constraints).get());
        EXPECT_EQ(MAGMA_STATUS_OK,
                  constraints->SetImageFormatConstraints(1, &image_constraints).get());
        EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());

        std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
        EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferDescription(&description).get());
        EXPECT_FALSE(description->is_secure);
        EXPECT_EQ(1u, description->count);

        uint32_t handle;
        uint32_t offset;
        EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferHandle(0u, &handle, &offset).get());

        auto platform_handle = magma::PlatformHandle::Create(handle);
        EXPECT_NE(nullptr, platform_handle);
    }

    static void TestIntelTiling()
    {
        auto connection = magma_sysmem::PlatformSysmemConnection::Create();

        ASSERT_NE(nullptr, connection.get());

        uint32_t token;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->CreateBufferCollectionToken(&token).get());
        std::unique_ptr<magma_sysmem::PlatformBufferCollection> collection;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->ImportBufferCollection(token, &collection).get());

        magma_buffer_format_constraints_t buffer_constraints{};
        buffer_constraints.count = 1;
        buffer_constraints.usage = 0;
        buffer_constraints.secure_permitted = false;
        buffer_constraints.secure_required = false;
        buffer_constraints.cpu_domain_supported = true;
        buffer_constraints.min_size_bytes = 0;

        std::unique_ptr<magma_sysmem::PlatformBufferConstraints> constraints;
        EXPECT_EQ(MAGMA_STATUS_OK,
                  connection->CreateBufferConstraints(&buffer_constraints, &constraints).get());

        // Create Intel X-tiling
        magma_image_format_constraints_t image_constraints{};
        image_constraints.image_format = MAGMA_FORMAT_R8G8B8A8;
        image_constraints.has_format_modifier = true;
        image_constraints.format_modifier = MAGMA_FORMAT_MODIFIER_INTEL_X_TILED;
        image_constraints.width = 512;
        image_constraints.height = 512;
        image_constraints.layers = 1;
        image_constraints.bytes_per_row_divisor = 1;
        image_constraints.min_bytes_per_row = 0;

        EXPECT_EQ(MAGMA_STATUS_OK,
                  constraints->SetImageFormatConstraints(0, &image_constraints).get());
        EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());
        std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
        EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferDescription(&description).get());
        EXPECT_TRUE(description->has_format_modifier);
        EXPECT_EQ(MAGMA_FORMAT_MODIFIER_INTEL_X_TILED, description->format_modifier);
    }

    static void TestBuffer()
    {
        auto connection = magma_sysmem::PlatformSysmemConnection::Create();

        ASSERT_NE(nullptr, connection.get());

        uint32_t token;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->CreateBufferCollectionToken(&token).get());
        std::unique_ptr<magma_sysmem::PlatformBufferCollection> collection;
        EXPECT_EQ(MAGMA_STATUS_OK, connection->ImportBufferCollection(token, &collection).get());

        magma_buffer_format_constraints_t buffer_constraints{};
        buffer_constraints.count = 2;
        buffer_constraints.usage = 0;
        buffer_constraints.secure_permitted = false;
        buffer_constraints.secure_required = false;
        buffer_constraints.cpu_domain_supported = true;
        buffer_constraints.min_size_bytes = 1024;

        std::unique_ptr<magma_sysmem::PlatformBufferConstraints> constraints;
        EXPECT_EQ(MAGMA_STATUS_OK,
                  connection->CreateBufferConstraints(&buffer_constraints, &constraints).get());

        EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());
        std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
        EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferDescription(&description).get());

        EXPECT_FALSE(description->has_format_modifier);
        EXPECT_EQ(2u, description->count);
    }
};

TEST(PlatformSysmemConnection, CreateBuffer) { TestPlatformSysmemConnection::TestCreateBuffer(); }

TEST(PlatformSysmemConnection, SetConstraints)
{
    TestPlatformSysmemConnection::TestSetConstraints();
}

TEST(PlatformSysmemConnection, IntelTiling) { TestPlatformSysmemConnection::TestIntelTiling(); }

TEST(PlatformSysmemConnection, Buffer) { TestPlatformSysmemConnection::TestBuffer(); }
