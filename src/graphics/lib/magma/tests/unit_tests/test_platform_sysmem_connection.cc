// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

#include <array>

#include <gtest/gtest.h>

#include "platform_handle.h"
#include "platform_sysmem_connection.h"

class TestPlatformSysmemConnection {
 public:
  static void TestCreateBuffer() {
    auto connection = CreateConnection();

    ASSERT_NE(nullptr, connection.get());

    std::unique_ptr<magma::PlatformBuffer> buffer;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->AllocateBuffer(0, 16384, &buffer));
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_LE(16384u, buffer->size());
  }

  static void TestCreateBufferWithName() {
    auto connection = CreateConnection();

    ASSERT_NE(nullptr, connection.get());

    std::unique_ptr<magma::PlatformBuffer> buffer;
    EXPECT_EQ(MAGMA_STATUS_OK,
              connection->AllocateBuffer(MAGMA_SYSMEM_FLAG_FOR_CLIENT, 16384, &buffer));
    ASSERT_TRUE(buffer != nullptr);
    EXPECT_LE(16384u, buffer->size());
    uint32_t handle;
    EXPECT_TRUE(buffer->duplicate_handle(&handle));
    auto platform_handle = magma::PlatformHandle::Create(handle);
    EXPECT_TRUE(platform_handle);

    EXPECT_EQ("MagmaUnprotectedSysmemForClient", platform_handle->GetName());
  }

  static void TestSetConstraints() {
    auto connection = CreateConnection();

    ASSERT_NE(nullptr, connection.get());

    uint32_t token;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->CreateBufferCollectionToken(&token).get());
    std::unique_ptr<magma_sysmem::PlatformBufferCollection> collection;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->ImportBufferCollection(token, &collection).get());

    magma_buffer_format_constraints_t buffer_constraints = get_standard_buffer_constraints();

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

    EXPECT_NE(MAGMA_STATUS_OK, constraints->SetImageFormatConstraints(1, &image_constraints).get());
    EXPECT_EQ(MAGMA_STATUS_OK, constraints->SetImageFormatConstraints(0, &image_constraints).get());
    EXPECT_EQ(MAGMA_STATUS_OK, constraints->SetImageFormatConstraints(1, &image_constraints).get());
    EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());

    std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
    EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferDescription(&description).get());
    EXPECT_FALSE(description->is_secure());
    EXPECT_EQ(1u, description->count());
    magma_image_plane_t planes[MAGMA_MAX_IMAGE_PLANES] = {};
    EXPECT_TRUE(description->GetPlanes(128, 128, planes));
    EXPECT_EQ(128u * 4, planes[0].bytes_per_row);

    uint32_t handle;
    uint32_t offset;
    EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferHandle(0u, &handle, &offset).get());
    uint32_t color_space;
    EXPECT_TRUE(description->GetColorSpace(&color_space));
    EXPECT_EQ(MAGMA_COLORSPACE_SRGB, color_space);

    auto platform_handle = magma::PlatformHandle::Create(handle);
    EXPECT_NE(nullptr, platform_handle);
    EXPECT_EQ(0u, platform_handle->GetName().find("MagmaUnprotectedSysmemShared"));
  }

  static void TestI420() {
    auto connection = CreateConnection();

    ASSERT_NE(nullptr, connection.get());

    uint32_t token;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->CreateBufferCollectionToken(&token).get());
    std::unique_ptr<magma_sysmem::PlatformBufferCollection> collection;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->ImportBufferCollection(token, &collection).get());

    magma_buffer_format_constraints_t buffer_constraints = get_standard_buffer_constraints();

    std::unique_ptr<magma_sysmem::PlatformBufferConstraints> constraints;
    EXPECT_EQ(MAGMA_STATUS_OK,
              connection->CreateBufferConstraints(&buffer_constraints, &constraints).get());

    uint32_t in_color_space = MAGMA_COLORSPACE_REC709;
    EXPECT_NE(MAGMA_STATUS_OK, constraints->SetColorSpaces(0, 1, &in_color_space).get());

    magma_image_format_constraints_t image_constraints{};
    image_constraints.image_format = MAGMA_FORMAT_I420;
    image_constraints.has_format_modifier = false;
    image_constraints.format_modifier = 0;
    image_constraints.width = 512;
    image_constraints.height = 512;
    image_constraints.layers = 1;
    image_constraints.bytes_per_row_divisor = 1;
    image_constraints.min_bytes_per_row = 0;

    EXPECT_EQ(MAGMA_STATUS_OK, constraints->SetImageFormatConstraints(0, &image_constraints).get());

    EXPECT_EQ(MAGMA_STATUS_OK, constraints->SetColorSpaces(0, 1, &in_color_space).get());
    EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());

    std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
    EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferDescription(&description).get());
    magma_image_plane_t planes[MAGMA_MAX_IMAGE_PLANES] = {};
    constexpr uint32_t kImageWidth = 128;
    constexpr uint32_t kImageHeight = 128;
    EXPECT_TRUE(description->GetPlanes(kImageWidth, kImageHeight, planes));
    EXPECT_EQ(kImageWidth, planes[0].bytes_per_row);
    EXPECT_EQ(kImageWidth / 2, planes[1].bytes_per_row);
    EXPECT_EQ(kImageWidth / 2, planes[2].bytes_per_row);
    EXPECT_EQ(0u, planes[0].byte_offset);
    EXPECT_EQ(kImageWidth * kImageHeight, planes[1].byte_offset);
    EXPECT_EQ(kImageWidth * kImageHeight + (kImageWidth / 2) * (kImageHeight / 2),
              planes[2].byte_offset);

    uint32_t handle;
    uint32_t offset;
    EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferHandle(0u, &handle, &offset).get());

    auto platform_handle = magma::PlatformHandle::Create(handle);
    EXPECT_NE(nullptr, platform_handle);

    uint32_t color_space;
    EXPECT_TRUE(description->GetColorSpace(&color_space));
    // Could be one of a variety of color spaces.
    EXPECT_EQ(in_color_space, color_space);
  }

  static void TestIntelTiling() {
    auto connection = CreateConnection();

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

    EXPECT_EQ(MAGMA_STATUS_OK, constraints->SetImageFormatConstraints(0, &image_constraints).get());
    EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());
    std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
    EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferDescription(&description).get());
    EXPECT_TRUE(description->has_format_modifier());
    EXPECT_EQ(MAGMA_FORMAT_MODIFIER_INTEL_X_TILED, description->format_modifier());
  }

  static void TestBuffer() {
    auto connection = CreateConnection();

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

    EXPECT_FALSE(description->has_format_modifier());
    EXPECT_EQ(2u, description->count());
    EXPECT_EQ(MAGMA_FORMAT_INVALID, description->format());
  }

  static void TestProtectedBuffer() {
    auto connection = CreateConnection();

    ASSERT_NE(nullptr, connection.get());

    uint32_t token;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->CreateBufferCollectionToken(&token).get());
    std::unique_ptr<magma_sysmem::PlatformBufferCollection> collection;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->ImportBufferCollection(token, &collection).get());

    magma_buffer_format_constraints_t buffer_constraints{};
    buffer_constraints.count = 1;
    buffer_constraints.usage = 0;
    buffer_constraints.secure_permitted = true;
    buffer_constraints.secure_required = true;
    buffer_constraints.min_size_bytes = 1024;

    std::unique_ptr<magma_sysmem::PlatformBufferConstraints> constraints;
    EXPECT_EQ(MAGMA_STATUS_OK,
              connection->CreateBufferConstraints(&buffer_constraints, &constraints).get());

    EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());
    std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
    magma_status_t status = collection->GetBufferDescription(&description).get();
    if (status == MAGMA_STATUS_INTERNAL_ERROR) {
      printf(
          "GetBufferDescription returned internal error, possibly due to"
          "system not having protected memory. Skipping test\n");
      GTEST_SKIP();
      return;
    }

    EXPECT_EQ(MAGMA_COHERENCY_DOMAIN_INACCESSIBLE, description->coherency_domain());
  }

  static void TestProtectedBufferBadConstraints() {
    auto connection = CreateConnection();

    ASSERT_NE(nullptr, connection.get());

    uint32_t token;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->CreateBufferCollectionToken(&token).get());
    std::unique_ptr<magma_sysmem::PlatformBufferCollection> collection;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->ImportBufferCollection(token, &collection).get());

    magma_buffer_format_constraints_t buffer_constraints{};
    buffer_constraints.count = 1;
    buffer_constraints.usage = 0;
    buffer_constraints.secure_permitted = true;
    buffer_constraints.secure_required = true;
    buffer_constraints.ram_domain_supported = true;
    buffer_constraints.min_size_bytes = 1024;

    std::unique_ptr<magma_sysmem::PlatformBufferConstraints> constraints;
    EXPECT_EQ(MAGMA_STATUS_OK,
              connection->CreateBufferConstraints(&buffer_constraints, &constraints).get());

    EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());
    std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
    magma_status_t status = collection->GetBufferDescription(&description).get();
    // ram_domain_supported = true with secure_required isn't allowed.
    EXPECT_EQ(MAGMA_STATUS_INTERNAL_ERROR, status);
  }

  static void TestGetFormatIndex() {
    auto connection = CreateConnection();

    ASSERT_NE(nullptr, connection.get());

    uint32_t token;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->CreateBufferCollectionToken(&token).get());
    std::unique_ptr<magma_sysmem::PlatformBufferCollection> collection;
    EXPECT_EQ(MAGMA_STATUS_OK, connection->ImportBufferCollection(token, &collection).get());

    magma_buffer_format_constraints_t buffer_constraints = get_standard_buffer_constraints();

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

    magma_image_format_constraints_t nv12_image_constraints = image_constraints;
    nv12_image_constraints.image_format = MAGMA_FORMAT_NV12;

    EXPECT_EQ(MAGMA_STATUS_OK, constraints->SetImageFormatConstraints(0, &image_constraints).get());
    EXPECT_EQ(MAGMA_STATUS_OK,
              constraints->SetImageFormatConstraints(1, &nv12_image_constraints).get());
    EXPECT_EQ(MAGMA_STATUS_OK, constraints->SetImageFormatConstraints(2, &image_constraints).get());
    EXPECT_EQ(MAGMA_STATUS_OK,
              constraints->SetImageFormatConstraints(3, &nv12_image_constraints).get());
    EXPECT_EQ(MAGMA_STATUS_OK, collection->SetConstraints(constraints.get()).get());

    std::unique_ptr<magma_sysmem::PlatformBufferDescription> description;
    EXPECT_EQ(MAGMA_STATUS_OK, collection->GetBufferDescription(&description).get());

    std::array<magma_bool_t, 32> format_valid;
    for (uint32_t i = 0; i < format_valid.size(); ++i) {
      format_valid[i] = i & 1;
    }
    constexpr uint32_t kShortArraySize = 1;
    EXPECT_FALSE(
        description->GetFormatIndex(constraints.get(), format_valid.data(), kShortArraySize));
    for (uint32_t i = 0; i < format_valid.size(); ++i) {
      // Values shouldn't be modified.
      EXPECT_EQ(static_cast<bool>(i & 1), static_cast<bool>(format_valid[i])) << i;
    }

    EXPECT_TRUE(
        description->GetFormatIndex(constraints.get(), format_valid.data(), format_valid.size()));
    for (uint32_t i = 4; i < format_valid.size(); ++i) {
      EXPECT_FALSE(format_valid[i]);
    }

    // RGBA format constraints are identical, so the results should be the same.
    EXPECT_EQ(format_valid[0], format_valid[2]);
    // NV12 format constraints are identical, so the results should be the same.
    EXPECT_EQ(format_valid[1], format_valid[3]);
    // Format must be one of either NV12 or RGBA.
    EXPECT_NE(static_cast<bool>(format_valid[0]), static_cast<bool>(format_valid[1]));
  }

 private:
  static std::unique_ptr<magma_sysmem::PlatformSysmemConnection> CreateConnection() {
    zx::channel client_end, server_end;
    EXPECT_EQ(ZX_OK, zx::channel::create(0, &client_end, &server_end));
    EXPECT_EQ(ZX_OK, fdio_service_connect("/svc/fuchsia.sysmem.Allocator", server_end.release()));

    return magma_sysmem::PlatformSysmemConnection::Import(client_end.release());
  }

  static magma_buffer_format_constraints_t get_standard_buffer_constraints() {
    magma_buffer_format_constraints_t buffer_constraints{};
    buffer_constraints.count = 1;
    buffer_constraints.usage = 0;
    buffer_constraints.secure_permitted = false;
    buffer_constraints.secure_required = false;
    buffer_constraints.cpu_domain_supported = true;
    buffer_constraints.min_size_bytes = 0;
    return buffer_constraints;
  }
};

TEST(PlatformSysmemConnection, CreateBuffer) { TestPlatformSysmemConnection::TestCreateBuffer(); }
TEST(PlatformSysmemConnection, CreateBufferWithName) {
  TestPlatformSysmemConnection::TestCreateBufferWithName();
}

TEST(PlatformSysmemConnection, SetConstraints) {
  TestPlatformSysmemConnection::TestSetConstraints();
}

TEST(PlatformSysmemConnection, I420) { TestPlatformSysmemConnection::TestI420(); }
TEST(PlatformSysmemConnection, IntelTiling) { TestPlatformSysmemConnection::TestIntelTiling(); }

TEST(PlatformSysmemConnection, Buffer) { TestPlatformSysmemConnection::TestBuffer(); }

TEST(PlatformSysmemConnection, ProtectedBuffer) {
  TestPlatformSysmemConnection::TestProtectedBuffer();
}

TEST(PlatformSysmemConnection, ProtectedBufferBadConstraints) {
  TestPlatformSysmemConnection::TestProtectedBufferBadConstraints();
}

TEST(PlatformSysmemConnection, GetFormatIndex) {
  TestPlatformSysmemConnection::TestGetFormatIndex();
}
