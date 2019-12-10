// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/host_image.h"

#include "gtest/gtest.h"
#include "lib/images/cpp/images.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"

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

    FXL_DCHECK(!listener);

    listener = std::make_unique<ImageFactoryListener>(context.escher_image_factory);
    context.escher_image_factory = listener.get();

    return context;
  }

  std::unique_ptr<ImageFactoryListener> listener;
};

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

  fidl::InterfacePtr<fuchsia::images::ImagePipe> image_pipe;
  ASSERT_TRUE(Apply(scenic::NewCreateImagePipeCmd(kImagePipeId, image_pipe.NewRequest())));

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

VK_TEST_F(HostImageTest, RgbaImport) {
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
    FXL_LOG(INFO) << "Could not find UMA compatible memory pool, aborting test.";
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

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
