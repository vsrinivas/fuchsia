// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/host_image.h"

#include <gtest/gtest.h>

#include "lib/images/cpp/images.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_util.h"

using namespace escher;

namespace {

const uint32_t kVmoSize = 65536;
// If you change the size of this buffer, make sure that the YUV test in
// scenic_pixel_test.cc is also updated. Unlike this unit test,
// scenic_pixel_test.cc has no way to confirm that it is going through the
// direct-to-GPU path.
// TODO(SCN-1387): This number needs to be queried via sysmem or vulkan.
const uint32_t kSize = 64;
const uint32_t kMemoryId = 1;
const uint32_t kImageId = 2;
const uint32_t kImagePipeId = 3;

class ImageFactoryListener : public ImageFactory {
 public:
  ImageFactoryListener(ImageFactory* factory) : factory_(factory) {}
  ImagePtr NewImage(const ImageInfo& info, GpuMemPtr* out_ptr = nullptr) {
    ++images_created_;
    return factory_->NewImage(info, out_ptr);
  }

  uint32_t images_created_ = 0;
  ImageFactory* factory_;
};

}  // namespace

namespace scenic_impl {
namespace gfx {
namespace test {

class HostImageTest : public VkSessionTest {
 public:
  void TearDown() override {
    VkSessionTest::TearDown();
    listener.reset();
  }

  SessionContext CreateSessionContext() override {
    auto context = VkSessionTest::CreateSessionContext();

    FX_DCHECK(!listener);

    listener = std::make_unique<ImageFactoryListener>(context.escher_image_factory);
    context.escher_image_factory = listener.get();

    return context;
  }

  std::unique_ptr<ImageFactoryListener> listener;
};

// Test to make sure the Vulkan driver does not crash when we import
// the same vmo twice.
VK_TEST_F(HostImageTest, DupVmoHostTest) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  zx::vmo dup_vmo;
  status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  ASSERT_EQ(status, ZX_OK);

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(vmo), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId + 1, std::move(dup_vmo), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));
}

// Test to make sure the Vulkan driver does not crash when we import
// the same vmo twice when the vmo is using device memory.
VK_TEST_F(HostImageTest, DupVmoGPUTest) {
  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  // Create an VkImage and allocate exportable memory for that image.
  //
  // TODO(fxbug.dev/54153): Currently, on some platforms (like Fuchsia Emulator), only
  // VkDeviceMemory dedicated to VkImages can be exportable.
  //
  // In order to make exportable VMO allocation possible for all platforms
  // where we run this test, we'll allocate image dedicated memory if it is
  // necessary (by checking image memory requirements).
  //
  // |AllocateExportableMemoryDedicatedToImageIfRequired()| will allocate an
  // image-dedicated memory only if it is required, otherwise it will allocate
  // non dedicated memory instead.

  // We create an image of size 256 x 64. The size of VMO is expected to
  // be no less than 65536 bytes.
  const uint32_t kWidth = 256u;
  const uint32_t kHeight = 64u;
  const uint32_t kExpectedVmoSize = kWidth * kHeight * 4u;
  escher::ImageInfo image_info = {
      .format = vk::Format::eR8G8B8A8Srgb,
      .width = kWidth,
      .height = kHeight,
      .sample_count = 1,
      .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
      .memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal,
      .tiling = vk::ImageTiling::eOptimal,
      .is_external = true};
  vk::Image image =
      escher::image_utils::CreateVkImage(device, image_info, vk::ImageLayout::eUndefined);

  MemoryAllocationResult allocation_result = AllocateExportableMemoryDedicatedToImageIfRequired(
      device, physical_device, kExpectedVmoSize, image, vk::MemoryPropertyFlagBits::eDeviceLocal,
      vulkan_queues->dispatch_loader());
  vk::DeviceMemory memory = allocation_result.device_memory;
  // Import valid Vulkan device memory into Scenic.
  zx::vmo vmo = ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);

  zx::vmo dup_vmo;
  auto status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup_vmo);
  ASSERT_EQ(status, ZX_OK);

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(vmo), kExpectedVmoSize,
                                               fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId + 1, std::move(dup_vmo), kExpectedVmoSize,
                                               fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));
  device.freeMemory(memory);
  device.destroyImage(image);
}

VK_TEST_F(HostImageTest, FindResource) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(vmo), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));

  fuchsia::images::ImageInfo image_info{
      .width = kSize,
      .height = kSize,
      .stride = static_cast<uint32_t>(
          kSize * images::StrideBytesPerWidthPixel(fuchsia::images::PixelFormat::BGRA_8)),
      .pixel_format = fuchsia::images::PixelFormat::BGRA_8,
  };

  ASSERT_TRUE(Apply(scenic::NewCreateImageCmd(kImageId, kMemoryId, 0, image_info)));

  fidl::InterfacePtr<fuchsia::images::ImagePipe2> image_pipe;
  ASSERT_TRUE(Apply(scenic::NewCreateImagePipe2Cmd(kImagePipeId, image_pipe.NewRequest())));

  // Host images should be findable as their concrete sub-class.
  auto host_image_resource = FindResource<HostImage>(kImageId);
  EXPECT_TRUE(host_image_resource);
  // Host images should also be findable as their base class (i.e., Image).
  auto image_resource = FindResource<Image>(kImageId);
  EXPECT_TRUE(image_resource);
  // Memory should not be findable as the same base class.
  auto memory_as_image_resource = FindResource<Image>(kMemoryId);
  EXPECT_FALSE(memory_as_image_resource);
  // Image pipes should not be findable as the Image class (even though they are
  // an ImageBase, the next class down).
  auto image_pipe_as_image_resource = FindResource<Image>(kImagePipeId);
  EXPECT_FALSE(image_pipe_as_image_resource);
}

VK_TEST_F(HostImageTest, BgraImport) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(vmo), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));

  fuchsia::images::ImageInfo image_info{
      .width = kSize,
      .height = kSize,
      .stride = static_cast<uint32_t>(
          kSize * images::StrideBytesPerWidthPixel(fuchsia::images::PixelFormat::BGRA_8)),
      .pixel_format = fuchsia::images::PixelFormat::BGRA_8,
  };

  ASSERT_TRUE(Apply(scenic::NewCreateImageCmd(kImageId, kMemoryId, 0, image_info)));

  auto image_resource = FindResource<HostImage>(kImageId);
  ASSERT_TRUE(image_resource);

  EXPECT_FALSE(image_resource->IsDirectlyMapped());
  // Before updating pixels, image resources should never return a valid Escher
  // image.
  EXPECT_FALSE(image_resource->GetEscherImage());
  // Updating shouldn't crash when passesd a null gpu_uploader, but it should
  // also keep the image dirty, because the copy from CPU to GPU memory has not
  // occured yet.
  image_resource->UpdateEscherImage(/* gpu_uploader */ nullptr,
                                    /* image_layout_uploader */ nullptr);
  // Because we did not provide a valid batch uploader, the image is still dirty
  // and in need of an update. Until that succeeds, GetEscherImage() should not
  // return a valid image.
  EXPECT_FALSE(image_resource->GetEscherImage());
  // A backing image should have been constructed through the image factory.
  EXPECT_EQ(1u, listener->images_created_);
}

VK_TEST_F(HostImageTest, YuvImportOnUmaPlatform) {
  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  if (!Memory::HasSharedMemoryPools(device, physical_device)) {
    FX_LOGS(INFO) << "Could not find UMA compatible memory pool, aborting test.";
    return;
  }

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(vmo), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));

  fuchsia::images::ImageInfo image_info{
      .width = kSize,
      .height = kSize,
      .stride = static_cast<uint32_t>(
          kSize * images::StrideBytesPerWidthPixel(fuchsia::images::PixelFormat::NV12)),
      .pixel_format = fuchsia::images::PixelFormat::NV12,
  };

  ASSERT_TRUE(Apply(scenic::NewCreateImageCmd(kImageId, kMemoryId, 0, image_info)));

  auto image_resource = FindResource<HostImage>(kImageId);
  ASSERT_TRUE(image_resource);

  EXPECT_TRUE(image_resource->IsDirectlyMapped());
  // For direct mapped images, when we create the image, the Escher image will
  // be created as well.
  EXPECT_TRUE(image_resource->GetEscherImage());
  // Updating should be a no-op, so it shouldn't crash when passesd a null
  // gpu_uploader, but it should also remove the dirty bit, meaning there is no
  // additional work to do.
  image_resource->UpdateEscherImage(/* gpu_uploader */ nullptr,
                                    /* image_layout_uploader */ nullptr);
  // Despite not updating, the resource should have a valid Escher image, since
  // we mapped it directly with zero copies.
  EXPECT_TRUE(image_resource->GetEscherImage());
  // The images should have been constructed directly, not through the image
  // factory.
  EXPECT_EQ(0u, listener->images_created_);
}

VK_TEST_F(HostImageTest, RgbaImportFails) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(vmo), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));

  fuchsia::images::ImageInfo image_info{
      .width = kSize,
      .height = kSize,
      .stride = static_cast<uint32_t>(
          kSize * images::StrideBytesPerWidthPixel(fuchsia::images::PixelFormat::R8G8B8A8)),
      .pixel_format = fuchsia::images::PixelFormat::R8G8B8A8,
  };

  // This should fail as host memory backed RGBA images are not supported.
  EXPECT_FALSE(Apply(scenic::NewCreateImageCmd(kImageId, kMemoryId, 0, image_info)));
  EXPECT_FALSE(FindResource<HostImage>(kImageId));
  EXPECT_EQ(0u, listener->images_created_);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
