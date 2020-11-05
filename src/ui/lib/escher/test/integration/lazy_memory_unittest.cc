// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/material/material.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/renderer/frame.h"
#include "src/ui/lib/escher/test/common/paper_renderer_test.h"

namespace escher {
namespace test {

static void DrawSceneContent(PaperRenderer* renderer, uint32_t width, uint32_t height) {
  const glm::vec4 kYellow(1, 1, 0, 1);
  const glm::vec4 kCyan75(0, 1, 1, 0.75);
  const glm::vec4 kBlack(0, 0, 0, 1);

  escher::PaperTransformStack* transform_stack = renderer->transform_stack();

  transform_stack->PushTranslation(vec2(0, 0));
  {
    transform_stack->PushElevation(0);
    vec2 top_left(0, 0);
    vec2 bottom_right(width, height);
    renderer->DrawRect(top_left, bottom_right, Material::New(kBlack));
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-2);
    vec2 top_left(width / 2, height / 2);
    vec2 bottom_right(width, height);
    renderer->DrawRect(top_left, bottom_right, Material::New(kYellow));
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-1);
    vec2 top_left(0, 0);
    vec2 bottom_right(width * 3 / 4, height * 3 / 4);
    MaterialPtr material = Material::New(kCyan75);
    material->set_type(Material::Type::kTranslucent);
    renderer->DrawRect(top_left, bottom_right, material);
    transform_stack->Pop();
  }
}

// TODO(http://fxbug.dev/63702): vkGetDeviceMemoryCommitment() is not properly implemented; it
// returns non-zero commitment even when `fx shell memgraph -v` shows that the corresponding VMO
// has not committed memory.  Hence, this test is disabled.  Note that, even after 63696 is fixed,
// this test is overly optimistic.  For example, Vulkan doens't *guarantee* that lazily-allocated
// memory won't actually be allocated (that's why it's called "lazily allocated", not
// "unallocated").  Therefore this test may need to be adjusted on certain platforms (under certain
// as-yet-unknown circumstances), to not fail.
VK_TEST_F(PaperRendererTest, DISABLED_TransientDepthStencilAndMsaaAttachments) {
  PaperRendererConfig config;
  config.num_depth_buffers = 3;

  {
    SetupFrame();
    renderer()->SetConfig(config);
    BeginRenderingFrame();
    DrawSceneContent(renderer(), kFramebufferWidth, kFramebufferHeight);
    EndRenderingFrame();
    escher()->vk_device().waitIdle();
    TeardownFrame();
    EXPECT_EQ(0U, renderer()->GetTransientImageMemoryCommitment());
  }

  config.shadow_type = PaperRendererShadowType::kShadowVolume;
  config.msaa_sample_count = 2;

  {
    SetupFrame();
    renderer()->SetConfig(config);
    BeginRenderingFrame();
    DrawSceneContent(renderer(), kFramebufferWidth, kFramebufferHeight);
    EndRenderingFrame();
    escher()->vk_device().waitIdle();
    TeardownFrame();
    EXPECT_EQ(0U, renderer()->GetTransientImageMemoryCommitment());
  }

  config.debug_frame_number = true;

  {
    SetupFrame();
    renderer()->SetConfig(config);
    BeginRenderingFrame();
    DrawSceneContent(renderer(), kFramebufferWidth, kFramebufferHeight);
    EndRenderingFrame();
    escher()->vk_device().waitIdle();
    TeardownFrame();
    EXPECT_EQ(0U, renderer()->GetTransientImageMemoryCommitment());
  }
}

}  // namespace test
}  // namespace escher
