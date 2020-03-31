// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/test/common/paper_renderer_test.h"

#include "src/ui/lib/escher/debug/debug_rects.h"
#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/scene/viewing_volume.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/types/color_histogram.h"
#include "src/ui/lib/escher/util/image_utils.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

void PaperRendererTest::SetUp() {
  ReadbackTest::SetUp();

  escher()->shader_program_factory()->filesystem()->InitializeWithRealFiles(
      kPaperRendererShaderPaths);
  PaperRendererConfig config;
  auto depth_stencil_format = escher()->device()->caps().GetMatchingDepthStencilFormat();
  if (depth_stencil_format.result == vk::Result::eSuccess) {
    config.depth_stencil_format = depth_stencil_format.value;
    FXL_LOG(INFO) << "Depth stencil format set to " << vk::to_string(config.depth_stencil_format);
  } else {
    GTEST_SKIP() << "Cannot find a valid depth stencil format, test skipped";
  }
  renderer_ = PaperRenderer::New(escher(), config);
}

void PaperRendererTest::TearDown() {
  renderer_.reset();
  ReadbackTest::TearDown();
}

void PaperRendererTest::SetupFrame() {
  frame_data_ = NewFrame(vk::ImageLayout::eColorAttachmentOptimal);
  gpu_uploader_ = std::make_shared<BatchGpuUploader>(escher(), frame_data_.frame->frame_number());

  scene_ = fxl::MakeRefCounted<PaperScene>();
  scene_->point_lights.resize(0);
  scene_->ambient_light.color = vec3(1, 1, 1);
  scene_->bounding_box =
      BoundingBox(vec3(0, 0, -200), vec3(kFramebufferWidth, kFramebufferHeight, 1));

  const escher::ViewingVolume& volume = ViewingVolume(scene_->bounding_box);
  escher::Camera cam = escher::Camera::NewOrtho(volume);
  cameras_ = {cam};
};

void PaperRendererTest::TeardownFrame() {
  frame_data_.frame->EndFrame(SemaphorePtr(), []() {});
}

void PaperRendererTest::BeginRenderingFrame() {
  renderer_->BeginFrame(frame_data_.frame, gpu_uploader_, scene_, cameras_,
                        frame_data_.color_attachment);
}

void PaperRendererTest::EndRenderingFrame() {
  renderer_->FinalizeFrame();
  auto upload_semaphore = escher::Semaphore::New(escher()->vk_device());
  gpu_uploader_->AddSignalSemaphore(upload_semaphore);
  gpu_uploader_->Submit();
  renderer_->EndFrame(upload_semaphore);
}

std::vector<uint8_t> PaperRendererTest::GetPixelData() {
  return ReadbackFromColorAttachment(frame_data_.frame, vk::ImageLayout::eColorAttachmentOptimal,
                                     vk::ImageLayout::eColorAttachmentOptimal);
};

}  // namespace test
}  // namespace escher
