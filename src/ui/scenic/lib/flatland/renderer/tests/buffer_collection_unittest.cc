// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/buffer_collection.h"

#include <lib/fdio/directory.h>

#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/scenic/lib/flatland/renderer/tests/common.h"

namespace escher {
namespace test {

using BufferCollectionTest = flatland::RendererTest;

// Simple test to make sure we can create a buffer collection from a token
// and that it is bound.
VK_TEST_F(BufferCollectionTest, CreateCollectionTest) {
  auto escher = GetEscher();
  auto vk_device = escher->vk_device();
  auto vk_loader = escher->device()->dispatch_loader();
  auto image_create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eUndefined);

  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());
  auto collection = flatland::BufferCollectionInfo::CreateWithConstraints(
      vk_device, vk_loader, sysmem_allocator_.get(), image_create_info,
      std::move(tokens.dup_token));
  EXPECT_TRUE(collection);

  EXPECT_TRUE(collection->GetSyncPtr());
  EXPECT_TRUE(collection->GetSyncPtr().is_bound());

  // Cleanup.
  collection->Destroy(vk_device, vk_loader);
}

// Check to make sure |CreateBufferCollectionAndSetConstraints| returns false if
// an invalid BufferCollectionHandle is provided by the user.
VK_TEST_F(BufferCollectionTest, NullTokenTest) {
  auto escher = GetEscher();
  auto vk_device = escher->vk_device();
  auto vk_loader = escher->device()->dispatch_loader();
  auto image_create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eUndefined);

  auto collection = flatland::BufferCollectionInfo::CreateWithConstraints(
      vk_device, vk_loader, sysmem_allocator_.get(), image_create_info, nullptr);
  EXPECT_FALSE(collection);
}

// We pass in a valid channel to |CreateBufferCollectionAndSetConstraints|, but
// it's not actually a channel to a BufferCollection.
VK_TEST_F(BufferCollectionTest, WrongTokenTypeTest) {
  auto escher = GetEscher();
  auto vk_device = escher->vk_device();
  auto vk_loader = escher->device()->dispatch_loader();
  auto image_create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eUndefined);

  zx::channel local_endpoint;
  zx::channel remote_endpoint;
  zx::channel::create(0, &local_endpoint, &remote_endpoint);

  // Here we inject a generic channel into a BufferCollectionHandle before passing the
  // handle into |CreateCollectionAndSetConstraints|. So the channel is valid,
  // but it is just not a BufferCollectionToken.
  flatland::BufferCollectionHandle handle{std::move(remote_endpoint)};

  // Make sure the handle is valid before passing it in.
  ASSERT_TRUE(handle.is_valid());

  // We should not be able to make a BufferCollectionInfon object with the wrong token type
  // passed in as a parameter.
  auto collection = flatland::BufferCollectionInfo::CreateWithConstraints(
      vk_device, vk_loader, sysmem_allocator_.get(), image_create_info, std::move(handle));
  EXPECT_FALSE(collection);
}

// If the client sets constraints on the buffer collection that are incompatible
// with the constraints set on the server-side by the renderer, then waiting on
// the buffers to be allocated should fail.
VK_TEST_F(BufferCollectionTest, IncompatibleConstraintsTest) {
  auto escher = GetEscher();
  auto vk_device = escher->vk_device();
  auto vk_loader = escher->device()->dispatch_loader();
  auto image_create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eUndefined);

  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  auto collection = flatland::BufferCollectionInfo::CreateWithConstraints(
      vk_device, vk_loader, sysmem_allocator_.get(), image_create_info,
      std::move(tokens.dup_token));
  EXPECT_TRUE(collection);
  EXPECT_TRUE(collection->GetSyncPtr());
  EXPECT_TRUE(collection->GetSyncPtr().is_bound());

  // Create a client-side handle to the buffer collection and set the client
  // constraints. We set it to have a max of zero buffers and to not use
  // vulkan sampling, which the server side will specify is necessary.
  {
    fuchsia::sysmem::BufferCollectionSyncPtr client_collection;
    zx_status_t status = sysmem_allocator_->BindSharedCollection(std::move(tokens.local_token),
                                                                 client_collection.NewRequest());
    EXPECT_EQ(status, ZX_OK);
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
  }

  // This should fail as sysmem won't be able to allocate anything.
  EXPECT_FALSE(collection->WaitUntilAllocated());

  // Cleanup.
  collection->Destroy(vk_device, vk_loader);
}

VK_TEST_F(BufferCollectionTest, DestructionTest) {
  auto escher = GetEscher();
  auto vk_device = escher->vk_device();
  auto vk_loader = escher->device()->dispatch_loader();
  auto image_create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eUndefined);

  // First create the buffer and ensure that its members have been instantiated properly.
  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());
  auto collection = flatland::BufferCollectionInfo::CreateWithConstraints(
      vk_device, vk_loader, sysmem_allocator_.get(), image_create_info,
      std::move(tokens.dup_token));
  EXPECT_TRUE(collection);

  EXPECT_TRUE(collection->GetSyncPtr());
  EXPECT_TRUE(collection->GetSyncPtr().is_bound());

  // Now delete the collection and ensure its members have been deleted properly.
  collection->Destroy(vk_device, vk_loader);
  EXPECT_EQ(collection->GetVkHandle(), vk::BufferCollectionFUCHSIA());
}

}  // namespace test
}  // namespace escher
