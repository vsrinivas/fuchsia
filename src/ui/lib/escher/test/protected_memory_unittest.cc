// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/scene/viewing_volume.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image_factory.h"

namespace {
using namespace escher;

// This function must be called after we set up the global EscherEnvironment, i.e. inside test body
// functions.
std::unique_ptr<Escher> GetEscherWithProtectedMemoryEnabled() {
  VulkanDeviceQueues::Params device_params(
      {{VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME}, {}, vk::SurfaceKHR()});
#ifdef OS_FUCHSIA
  device_params.required_extension_names.insert(VK_FUCHSIA_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
  device_params.flags = VulkanDeviceQueues::Params::kAllowProtectedMemory;
#endif
  auto vulkan_instance =
      escher::test::EscherEnvironment::GetGlobalTestEnvironment()->GetVulkanInstance();
  auto vulkan_device = VulkanDeviceQueues::New(vulkan_instance, device_params);
  auto escher = std::make_unique<Escher>(vulkan_device);
  if (!escher->allow_protected_memory()) {
    return nullptr;
  }
  return escher;
}

// Encapsulates boilerplate of rendering a simple scene using PaperRenderer.
void RenderFrameForProtectedMemoryTest(const PaperRendererPtr& renderer, const FramePtr& frame,
                                       const ImagePtr& image) {
  image->set_swapchain_layout(vk::ImageLayout::eColorAttachmentOptimal);

  // Create simple scene/camera.
  auto scene = fxl::MakeRefCounted<PaperScene>();
  scene->point_lights.resize(1);
  scene->bounding_box = BoundingBox(vec3(0), vec3(32));
  const escher::ViewingVolume& volume = ViewingVolume(scene->bounding_box);
  escher::Camera cam = escher::Camera::NewOrtho(volume);
  auto cameras = {cam};

  auto gpu_uploader = std::make_shared<escher::BatchGpuUploader>(frame->escher()->GetWeakPtr(),
                                                                 frame->frame_number());

  renderer->BeginFrame(frame, gpu_uploader, scene, cameras, image);
  renderer->DrawVLine(escher::DebugRects::kRed, 0, 0, 30, 1);

  renderer->FinalizeFrame();
  auto upload_semaphore = escher::Semaphore::New(frame->escher()->vk_device());
  gpu_uploader->AddSignalSemaphore(upload_semaphore);
  gpu_uploader->Submit();

  renderer->EndFrame(std::move(upload_semaphore));
  frame->EndFrame(SemaphorePtr(), [] {});
}

using ProtectedMemoryTest = escher::test::TestWithVkValidationLayer;

// Tests that we can create Escher with a protected Vk instance if platform supports.
VK_TEST_F(ProtectedMemoryTest, CreateProtectedEnabledEscher) {
  auto escher = GetEscherWithProtectedMemoryEnabled();
  EXPECT_TRUE(!escher || escher->allow_protected_memory());
}

// Tests that we can ask platform to provide protected enabled CommandBuffer.
VK_TEST_F(ProtectedMemoryTest, CreateProtectedEnabledCommandBuffer) {
  auto escher = GetEscherWithProtectedMemoryEnabled();
  if (!escher) {
    GTEST_SKIP();
  }

  auto cb = CommandBuffer::NewForType(escher.get(), CommandBuffer::Type::kGraphics,
                                      /*use_protected_memory=*/true);
  EXPECT_TRUE(cb->Submit(nullptr));
}

// Tests that we can create protected enabled Escher::Frame.
VK_TEST_F(ProtectedMemoryTest, CreateProtectedEnabledFrame) {
  auto escher = GetEscherWithProtectedMemoryEnabled();
  if (!escher) {
    GTEST_SKIP();
  }

  {
    auto frame = escher->NewFrame("test_frame", 0, false, escher::CommandBuffer::Type::kGraphics,
                                  /*use_protected_memory=*/true);
    frame->EndFrame(SemaphorePtr(), [] {});
  }
}

// Tests that we can send draw text via paper renderer using a protected frame.
VK_TEST_F(ProtectedMemoryTest, CreateProtectedEnabledPaperRenderer) {
  auto escher = GetEscherWithProtectedMemoryEnabled();
  if (!escher) {
    GTEST_SKIP();
  }

  auto renderer = PaperRenderer::New(escher->GetWeakPtr());

  auto protected_image = image_utils::NewImage(
      escher->image_cache(), vk::Format::eB8G8R8A8Unorm, 32, 32,
      vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc |
          vk::ImageUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eProtected);
  auto protected_frame =
      escher->NewFrame("test_frame", 0, false, escher::CommandBuffer::Type::kGraphics,
                       /*use_protected_memory=*/true);

  RenderFrameForProtectedMemoryTest(renderer, protected_frame, protected_image);

  escher->vk_device().waitIdle();
  ASSERT_TRUE(escher->Cleanup());
}

// Tests that we can send draw text via paper renderer using a protected frame after a regular draw
// call.
VK_TEST_F(ProtectedMemoryTest, PaperRendererSwitchToProtected) {
  auto escher = GetEscherWithProtectedMemoryEnabled();
  if (!escher) {
    GTEST_SKIP();
  }

  auto renderer = PaperRenderer::New(escher->GetWeakPtr());

  // Send a non-protected frame first.
  {
    auto image = image_utils::NewImage(escher->image_cache(), vk::Format::eB8G8R8A8Unorm, 32, 32,
                                       vk::ImageUsageFlagBits::eColorAttachment |
                                           vk::ImageUsageFlagBits::eTransferSrc |
                                           vk::ImageUsageFlagBits::eTransferDst);
    auto frame = escher->NewFrame("test_frame", 0, false, escher::CommandBuffer::Type::kGraphics);

    RenderFrameForProtectedMemoryTest(renderer, frame, image);
  }

  // Send a protected frame after.
  {
    auto protected_image = image_utils::NewImage(
        escher->image_cache(), vk::Format::eB8G8R8A8Unorm, 32, 32,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc |
            vk::ImageUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eProtected);
    auto protected_frame =
        escher->NewFrame("test_frame", 0, false, escher::CommandBuffer::Type::kGraphics,
                         /*use_protected_memory=*/true);

    RenderFrameForProtectedMemoryTest(renderer, protected_frame, protected_image);
  }

  escher->vk_device().waitIdle();
  ASSERT_TRUE(escher->Cleanup());
}

}  // namespace
