// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/render_pass_cache.h"

#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/gtest_escher.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/vk/render_pass_info.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace {
using namespace escher;

void CompareRenderPassWithInfo(const impl::RenderPassPtr& render_pass,
                               const RenderPassInfo& info) {
  EXPECT_EQ(render_pass->num_color_attachments(), info.num_color_attachments);
  const uint32_t num_subpasses = render_pass->num_subpasses();

  if (info.subpasses.size() == 0) {
    // Vulkan requires at least one subpass per render pass, so if none is
    // provided, then a default is created.
    ASSERT_EQ(num_subpasses, 1U);
    EXPECT_EQ(render_pass->GetInputAttachmentCountForSubpass(0), 0U);
    EXPECT_EQ(render_pass->GetColorAttachmentCountForSubpass(0),
              info.num_color_attachments);
  } else {
    // Subpasses are explicitly specified in the RenderPassInfo.
    ASSERT_EQ(num_subpasses, info.subpasses.size());
    for (uint32_t i = 0; i < num_subpasses; ++i) {
      EXPECT_EQ(render_pass->GetColorAttachmentCountForSubpass(i),
                info.subpasses[i].num_color_attachments);
      EXPECT_EQ(render_pass->GetInputAttachmentCountForSubpass(i),
                info.subpasses[i].num_input_attachments);
    }
  }
}

VK_TEST(RenderPassCache, DefaultSubpass) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  EXPECT_EQ(cache.size(), 0U);

  uint32_t width = 1024;
  uint32_t height = 1024;

  TexturePtr depth_tex1 = escher->NewAttachmentTexture(
      vk::Format::eD24UnormS8Uint, width, height, 1, vk::Filter::eNearest);
  TexturePtr depth_tex2 = escher->NewAttachmentTexture(
      vk::Format::eD24UnormS8Uint, width, height, 1, vk::Filter::eNearest);
  TexturePtr color_tex = escher->NewAttachmentTexture(
      vk::Format::eB8G8R8A8Unorm, width, height, 1, vk::Filter::eNearest);

  RenderPassInfo info;
  info.num_color_attachments = 1;
  info.color_attachments[0] = color_tex;
  info.depth_stencil_attachment = depth_tex1;

  CompareRenderPassWithInfo(cache.ObtainRenderPass(info), info);
  EXPECT_EQ(cache.size(), 1U);

  // The same RenderPass should be obtained if a different image is provided,
  // but with the same format.
  info.depth_stencil_attachment = depth_tex2;
  CompareRenderPassWithInfo(cache.ObtainRenderPass(info), info);
  EXPECT_EQ(cache.size(), 1U);

  // Using a different image format should result in a different RenderPass.
  depth_tex1 = escher->NewAttachmentTexture(vk::Format::eD32SfloatS8Uint, width,
                                            height, 1, vk::Filter::eNearest);
  info.depth_stencil_attachment = depth_tex1;
  CompareRenderPassWithInfo(cache.ObtainRenderPass(info), info);
  EXPECT_EQ(cache.size(), 2U);

  depth_tex1 = depth_tex2 = color_tex = nullptr;

  EXPECT_TRUE(escher->Cleanup());
}

}  // anonymous namespace
