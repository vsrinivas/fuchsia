// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/buffers/buffer_collection.h"

#include <lib/fdio/directory.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"

namespace flatland {
namespace test {

// Common testing base class to be used across different unittests that
// require Vulkan and a SysmemAllocator.
class BufferCollectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ::testing::Test::SetUp();
    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
    EXPECT_EQ(status, ZX_OK);
    sysmem_allocator_->SetDebugClientInfo(fsl::GetCurrentProcessName() + " BufferCollectionTest",
                                          fsl::GetCurrentProcessKoid());
  }

  void TearDown() override {
    sysmem_allocator_ = nullptr;
    ::testing::Test::TearDown();
  }

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

// Test the creation of a buffer collection that doesn't have any additional vulkan
// constraints to show that it doesn't need vulkan to be valid.
TEST_F(BufferCollectionTest, CreateCollectionTest) {
  auto tokens = SysmemTokens::Create(sysmem_allocator_.get());
  auto result = BufferCollectionInfo::New(sysmem_allocator_.get(), std::move(tokens.dup_token));
  EXPECT_TRUE(result.is_ok());
}

// This test ensures that the buffer collection can still be allocated even if the server
// does not set extra customizable constraints via a call to GenerateToken(). This is
// necessary due to the fact that the buffer collection keeps around a dummy token in
// case new constraints need to be added, but the existence of the dummy token itself
// prevents allocation until it is closed out. So this test makes sure that when we close
// out the dummy token inside the call to WaitUntilAllocated() that this is enough to ensure
// that we can still allocate the buffer collection.
TEST_F(BufferCollectionTest, AllocationWithoutExtraConstraints) {
  auto tokens = SysmemTokens::Create(sysmem_allocator_.get());
  auto result = BufferCollectionInfo::New(sysmem_allocator_.get(), std::move(tokens.dup_token));
  EXPECT_TRUE(result.is_ok());

  auto collection = std::move(result.value());

  // Client hasn't set their constraints yet, so this should be false.
  EXPECT_FALSE(collection.BuffersAreAllocated());

  {
    const uint32_t kWidth = 32;
    const uint32_t kHeight = 64;
    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    zx_status_t status = sysmem_allocator_->BindSharedCollection(std::move(tokens.local_token),
                                                                 buffer_collection.NewRequest());
    buffer_collection->SetName(100u, "FlatlandAllocationWithoutExtraConstraints");
    EXPECT_EQ(status, ZX_OK);
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints.cpu_domain_supported = true;
    constraints.buffer_memory_constraints.ram_domain_supported = true;
    constraints.usage.cpu = fuchsia::sysmem::cpuUsageWriteOften;
    constraints.min_buffer_count = 1;

    constraints.image_format_constraints_count = 1;
    auto& image_constraints = constraints.image_format_constraints[0];
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] =
        fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;

    image_constraints.required_min_coded_width = kWidth;
    image_constraints.required_min_coded_height = kHeight;
    image_constraints.required_max_coded_width = kWidth;
    image_constraints.required_max_coded_height = kHeight;
    image_constraints.max_coded_width = kWidth * 4;
    image_constraints.max_coded_height = kHeight;
    image_constraints.max_bytes_per_row = 0xffffffff;

    status = buffer_collection->SetConstraints(true, constraints);
    EXPECT_EQ(status, ZX_OK);

    // Have the client wait for allocation.
    zx_status_t allocation_status = ZX_OK;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
    status =
        buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);

    status = buffer_collection->Close();
    EXPECT_EQ(status, ZX_OK);
  }

  // Checking allocation on the server should return true.
  EXPECT_TRUE(collection.BuffersAreAllocated());
}

// Check to make sure |CreateBufferCollectionAndSetConstraints| returns false if
// an invalid BufferCollectionHandle is provided by the user.
TEST_F(BufferCollectionTest, NullTokenTest) {
  auto result = BufferCollectionInfo::New(sysmem_allocator_.get(),
                                          /*token*/ nullptr);
  EXPECT_TRUE(result.is_error());
}

// We pass in a valid channel to |CreateBufferCollectionAndSetConstraints|, but
// it's not actually a channel to a BufferCollection.
TEST_F(BufferCollectionTest, WrongTokenTypeTest) {
  zx::channel local_endpoint;
  zx::channel remote_endpoint;
  zx::channel::create(0, &local_endpoint, &remote_endpoint);

  // Here we inject a generic channel into a BufferCollectionHandle before passing the
  // handle into |CreateCollectionAndSetConstraints|. So the channel is valid,
  // but it is just not a BufferCollectionToken.
  BufferCollectionHandle handle{std::move(remote_endpoint)};

  // Make sure the handle is valid before passing it in.
  ASSERT_TRUE(handle.is_valid());

  // We should not be able to make a BufferCollectionInfon object with the wrong token type
  // passed in as a parameter.
  auto result = BufferCollectionInfo::New(sysmem_allocator_.get(), std::move(handle));
  EXPECT_TRUE(result.is_error());
}

// If the client sets constraints on the buffer collection that are incompatible
// with the constraints set on the server-side by the renderer, then waiting on
// the buffers to be allocated should fail.
TEST_F(BufferCollectionTest, IncompatibleConstraintsTest) {
  auto tokens = SysmemTokens::Create(sysmem_allocator_.get());
  auto result = BufferCollectionInfo::New(sysmem_allocator_.get(), std::move(tokens.dup_token));
  EXPECT_TRUE(result.is_ok());

  auto collection = std::move(result.value());

  // Create a client-side handle to the buffer collection and set the client
  // constraints. We set it to have a max of zero buffers and to not use
  // vulkan sampling, which the server side will specify is necessary.
  {
    fuchsia::sysmem::BufferCollectionSyncPtr client_collection;
    zx_status_t status = sysmem_allocator_->BindSharedCollection(std::move(tokens.local_token),
                                                                 client_collection.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    client_collection->SetName(100u, "FlatlandIncompatibleConstraintsTest");
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints.cpu_domain_supported = true;
    constraints.buffer_memory_constraints.ram_domain_supported = true;
    constraints.usage.cpu = fuchsia::sysmem::cpuUsageWriteOften;

    // Need at least one buffer normally.
    constraints.min_buffer_count = 0;
    constraints.max_buffer_count = 0;

    constraints.usage.vulkan = !fuchsia::sysmem::vulkanUsageSampled;

    constraints.image_format_constraints_count = 1;
    auto& image_constraints = constraints.image_format_constraints[0];
    image_constraints.color_spaces_count = 0;

    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;

    // The renderer requires that the the buffer can at least have a
    // width/height of 1, which is not possible here.
    image_constraints.required_min_coded_width = 0;
    image_constraints.required_min_coded_height = 0;
    image_constraints.required_max_coded_width = 0;
    image_constraints.required_max_coded_height = 0;
    image_constraints.max_coded_width = 0;
    image_constraints.max_coded_height = 0;
    image_constraints.max_bytes_per_row = 0x0;
    status = client_collection->SetConstraints(true, constraints);
    EXPECT_EQ(status, ZX_OK);

    // Have the client wait for allocation.
    zx_status_t allocation_status = ZX_OK;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
    status =
        client_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);

    // Sysmem reports the error here through |status|.
    EXPECT_NE(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
  }

  // This should fail as sysmem won't be able to allocate anything.
  EXPECT_FALSE(collection.BuffersAreAllocated());
}

}  // namespace test
}  // namespace flatland
