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

void CompareRenderPassWithInfo(const impl::RenderPassPtr& render_pass, const RenderPassInfo& info) {
  EXPECT_EQ(render_pass->num_color_attachments(), info.num_color_attachments);
  const uint32_t num_subpasses = render_pass->num_subpasses();

  if (info.subpasses.size() == 0) {
    // Vulkan requires at least one subpass per render pass, so if none is
    // provided, then a default is created.
    ASSERT_EQ(num_subpasses, 1U);
    EXPECT_EQ(render_pass->GetInputAttachmentCountForSubpass(0), 0U);
    EXPECT_EQ(render_pass->GetColorAttachmentCountForSubpass(0), info.num_color_attachments);
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

  TexturePtr depth_tex1 = escher->NewAttachmentTexture(vk::Format::eD24UnormS8Uint, width, height,
                                                       1, vk::Filter::eNearest);
  TexturePtr depth_tex2 = escher->NewAttachmentTexture(vk::Format::eD24UnormS8Uint, width, height,
                                                       1, vk::Filter::eNearest);
  TexturePtr color_tex = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height, 1,
                                                      vk::Filter::eNearest);

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
  depth_tex1 = escher->NewAttachmentTexture(vk::Format::eD32SfloatS8Uint, width, height, 1,
                                            vk::Filter::eNearest);
  info.depth_stencil_attachment = depth_tex1;
  CompareRenderPassWithInfo(cache.ObtainRenderPass(info), info);
  EXPECT_EQ(cache.size(), 2U);

  depth_tex1 = depth_tex2 = color_tex = nullptr;

  EXPECT_TRUE(escher->Cleanup());
}

// Helper function for RenderPassCache.RespectsSampleCount.
static void InitRenderPassInfo(RenderPassInfo* rp, const TexturePtr& depth_tex,
                               const TexturePtr& color_tex, const TexturePtr& resolve_tex) {
  const uint32_t width = depth_tex->width();
  const uint32_t height = depth_tex->height();
  const uint32_t sample_count = depth_tex->image()->info().sample_count;

  FXL_DCHECK(width == color_tex->width());
  FXL_DCHECK(height == color_tex->height());
  FXL_DCHECK(sample_count == color_tex->image()->info().sample_count);

  static constexpr uint32_t kRenderTargetAttachmentIndex = 0;
  static constexpr uint32_t kResolveTargetAttachmentIndex = 1;

  rp->depth_stencil_attachment = depth_tex;
  rp->color_attachments[kRenderTargetAttachmentIndex] = color_tex;
  rp->num_color_attachments = 1;
  // Clear and store color attachment 0, the sole color attachment.
  rp->clear_attachments = 1u << kRenderTargetAttachmentIndex;
  rp->store_attachments = 1u << kRenderTargetAttachmentIndex;
  // Standard flags for a depth-testing render-pass that needs to first clear
  // the depth image.
  rp->op_flags = RenderPassInfo::kClearDepthStencilOp | RenderPassInfo::kOptimalColorLayoutOp |
                 RenderPassInfo::kOptimalDepthStencilLayoutOp;
  rp->clear_color[0].setFloat32({0.f, 0.f, 0.f, 0.f});

  if (sample_count == 1) {
    FXL_DCHECK(!resolve_tex);

    rp->color_attachments[kRenderTargetAttachmentIndex] = color_tex;
    rp->subpasses.push_back(RenderPassInfo::Subpass{
        .num_color_attachments = 1,
        .color_attachments = {kRenderTargetAttachmentIndex},
        .num_input_attachments = 0,
        .input_attachments = {},
        .num_resolve_attachments = 0,
        .resolve_attachments = {},
    });
  } else {
    FXL_DCHECK(resolve_tex);
    FXL_DCHECK(resolve_tex->image()->info().sample_count == 1);
    FXL_DCHECK(width == resolve_tex->width());
    FXL_DCHECK(height == resolve_tex->height());

    rp->color_attachments[kResolveTargetAttachmentIndex] = resolve_tex;
    rp->num_color_attachments++;
    rp->subpasses.push_back(RenderPassInfo::Subpass{
        .num_color_attachments = 1,
        .color_attachments = {kRenderTargetAttachmentIndex},
        .num_input_attachments = 0,
        .input_attachments = {},
        .num_resolve_attachments = 1,
        .resolve_attachments = {kResolveTargetAttachmentIndex},
    });
  }
}

VK_TEST(RenderPassCache, RespectsSampleCount) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  EXPECT_EQ(cache.size(), 0U);

  uint32_t width = 1024;
  uint32_t height = 1024;

  // Attachments and renderpass info for no MSAA.
  TexturePtr depth_tex1 = escher->NewAttachmentTexture(vk::Format::eD24UnormS8Uint, width, height,
                                                       1, vk::Filter::eNearest);
  TexturePtr color_tex1a = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        1, vk::Filter::eNearest);
  TexturePtr color_tex1b = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        1, vk::Filter::eNearest);
  RenderPassInfo info1a, info1b;
  InitRenderPassInfo(&info1a, depth_tex1, color_tex1a, nullptr);
  InitRenderPassInfo(&info1b, depth_tex1, color_tex1b, nullptr);

  // Attachments and renderpass info for 2x MSAA.
  TexturePtr depth_tex2 = escher->NewAttachmentTexture(vk::Format::eD24UnormS8Uint, width, height,
                                                       2, vk::Filter::eNearest);
  TexturePtr color_tex2a = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        2, vk::Filter::eNearest);
  TexturePtr color_tex2b = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        2, vk::Filter::eNearest);
  RenderPassInfo info2a, info2b;
  InitRenderPassInfo(&info2a, depth_tex2, color_tex2a, color_tex1a);
  InitRenderPassInfo(&info2b, depth_tex2, color_tex2b, color_tex1b);

  // Attachments and renderpass info for 4x MSAA.
  TexturePtr depth_tex4 = escher->NewAttachmentTexture(vk::Format::eD24UnormS8Uint, width, height,
                                                       4, vk::Filter::eNearest);
  TexturePtr color_tex4a = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        4, vk::Filter::eNearest);
  TexturePtr color_tex4b = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        4, vk::Filter::eNearest);
  RenderPassInfo info4a, info4b;
  InitRenderPassInfo(&info4a, depth_tex4, color_tex4a, color_tex1a);
  InitRenderPassInfo(&info4b, depth_tex4, color_tex4b, color_tex1b);

  impl::RenderPassPtr rp1a, rp1b, rp2a, rp2b, rp4a, rp4b;
  rp1a = cache.ObtainRenderPass(info1a);
  rp1b = cache.ObtainRenderPass(info1b);
  rp2a = cache.ObtainRenderPass(info2a);
  rp2b = cache.ObtainRenderPass(info2b);
  rp4a = cache.ObtainRenderPass(info4a);
  rp4b = cache.ObtainRenderPass(info4b);

  // Same cached renderpass should be returned for info with the same sample count (but different
  // framebuffer images).
  EXPECT_EQ(rp1a, rp1b);
  EXPECT_EQ(rp2a, rp2b);
  EXPECT_EQ(rp4a, rp4b);

  // Different cached renderpass should be returned when the sample count differs.
  EXPECT_NE(rp1a, rp2a);
  EXPECT_NE(rp1a, rp4a);
  EXPECT_NE(rp2a, rp4a);

  EXPECT_TRUE(escher->Cleanup());
}

}  // anonymous namespace
