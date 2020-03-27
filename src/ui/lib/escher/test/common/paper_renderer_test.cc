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
  ren = PaperRenderer::New(escher(), config);
}

void PaperRendererTest::TearDown() {
  ren.reset();
  ReadbackTest::TearDown();
}

void PaperRendererTest::SetupFrame() {
  fd = NewFrame(vk::ImageLayout::eColorAttachmentOptimal);

  scene = fxl::MakeRefCounted<PaperScene>();
  scene->point_lights.resize(1);
  scene->bounding_box = BoundingBox(vec3(0), vec3(kFramebufferHeight));

  const escher::ViewingVolume& volume = ViewingVolume(scene->bounding_box);
  escher::Camera cam = escher::Camera::NewOrtho(volume);
  cameras = {cam};
};

std::vector<uint8_t> PaperRendererTest::GetFrameData() {
  return ReadbackFromColorAttachment(fd.frame, vk::ImageLayout::eColorAttachmentOptimal,
                                     vk::ImageLayout::eColorAttachmentOptimal);
};

}  // namespace test
}  // namespace escher
