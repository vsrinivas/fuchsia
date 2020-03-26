// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/impl/render_pass_cache.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
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

using RenderPassCacheTest = test::TestWithVkValidationLayer;

VK_TEST_F(RenderPassCacheTest, DefaultSubpass) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  EXPECT_EQ(cache.size(), 0U);

  uint32_t width = 1024;
  uint32_t height = 1024;

  // Get depth stencil texture format supported by the device.
  std::set<vk::Format> supported_depth_stencil_formats_set =
      escher->device()->caps().GetAllMatchingDepthStencilFormats(
          {vk::Format::eD16UnormS8Uint, vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint});
  std::vector<vk::Format> supported_depth_stencil_formats = std::vector(
      supported_depth_stencil_formats_set.begin(), supported_depth_stencil_formats_set.end());
  if (supported_depth_stencil_formats.empty()) {
    FXL_LOG(ERROR) << "No depth stencil format is supported on this device, test terminated.";
    EXPECT_TRUE(escher->Cleanup());
    return;
  }

  TexturePtr depth_tex1 = escher->NewAttachmentTexture(supported_depth_stencil_formats[0], width,
                                                       height, 1, vk::Filter::eNearest);
  TexturePtr depth_tex2 = escher->NewAttachmentTexture(supported_depth_stencil_formats[0], width,
                                                       height, 1, vk::Filter::eNearest);
  TexturePtr color_tex = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height, 1,
                                                      vk::Filter::eNearest);

  RenderPassInfo info;
  info.op_flags = RenderPassInfo::kOptimalColorLayoutOp;
  info.num_color_attachments = 1;
  info.color_attachments[0] = color_tex;
  info.depth_stencil_attachment = depth_tex1;
  RenderPassInfo::InitRenderPassAttachmentInfosFromImages(&info);

  CompareRenderPassWithInfo(cache.ObtainRenderPass(info, true), info);
  EXPECT_EQ(cache.size(), 1U);

  // The same RenderPass should be obtained if a different image is provided,
  // but with the same format.
  info.depth_stencil_attachment = depth_tex2;
  RenderPassInfo::InitRenderPassAttachmentInfosFromImages(&info);

  CompareRenderPassWithInfo(cache.ObtainRenderPass(info, true), info);
  EXPECT_EQ(cache.size(), 1U);

  // Using a different image format should result in a different RenderPass.
  // However we cannot test it if there is only one image format supported.
  if (supported_depth_stencil_formats.size() == 1) {
    FXL_LOG(ERROR) << "Only one depth stencil format is supported on this device, test terminated.";
  } else {
    depth_tex1 = escher->NewAttachmentTexture(supported_depth_stencil_formats[1], width, height, 1,
                                              vk::Filter::eNearest);
    info.depth_stencil_attachment = depth_tex1;
    RenderPassInfo::InitRenderPassAttachmentInfosFromImages(&info);

    CompareRenderPassWithInfo(cache.ObtainRenderPass(info, true), info);
    EXPECT_EQ(cache.size(), 2U);

    depth_tex1 = depth_tex2 = color_tex = nullptr;
  }

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
        .color_attachments = {kRenderTargetAttachmentIndex},
        .input_attachments = {},
        .resolve_attachments = {},
        .num_color_attachments = 1,
        .num_input_attachments = 0,
        .num_resolve_attachments = 0,
    });
  } else {
    FXL_DCHECK(resolve_tex);
    FXL_DCHECK(resolve_tex->image()->info().sample_count == 1);
    FXL_DCHECK(width == resolve_tex->width());
    FXL_DCHECK(height == resolve_tex->height());

    rp->color_attachments[kResolveTargetAttachmentIndex] = resolve_tex;
    rp->num_color_attachments++;
    rp->subpasses.push_back(RenderPassInfo::Subpass{
        .color_attachments = {kRenderTargetAttachmentIndex},
        .input_attachments = {},
        .resolve_attachments = {kResolveTargetAttachmentIndex},
        .num_color_attachments = 1,
        .num_input_attachments = 0,
        .num_resolve_attachments = 1,
    });
  }
  RenderPassInfo::InitRenderPassAttachmentInfosFromImages(rp);
}

VK_TEST_F(RenderPassCacheTest, RespectsSampleCount) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());
  EXPECT_EQ(cache.size(), 0U);

  uint32_t width = 1024;
  uint32_t height = 1024;

  // Get depth stencil texture format supported by the device.
  auto depth_stencil_texture_format_result =
      escher->device()->caps().GetMatchingDepthStencilFormat();
  if (depth_stencil_texture_format_result.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "No depth stencil format is supported on this device.";
    return;
  }

  // Attachments and renderpass info for no MSAA.
  TexturePtr depth_tex1 = escher->NewAttachmentTexture(depth_stencil_texture_format_result.value,
                                                       width, height, 1, vk::Filter::eNearest);
  TexturePtr color_tex1a = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        1, vk::Filter::eNearest);
  TexturePtr color_tex1b = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        1, vk::Filter::eNearest);
  RenderPassInfo info1a, info1b;
  bool sample_1_supported = false;
  if (depth_tex1 || (color_tex1a && color_tex1b)) {
    InitRenderPassInfo(&info1a, depth_tex1, color_tex1a, nullptr);
    InitRenderPassInfo(&info1b, depth_tex1, color_tex1b, nullptr);
    sample_1_supported = true;
  }

  // Attachments and renderpass info for 2x MSAA.
  TexturePtr depth_tex2 = escher->NewAttachmentTexture(depth_stencil_texture_format_result.value,
                                                       width, height, 2, vk::Filter::eNearest);
  TexturePtr color_tex2a = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        2, vk::Filter::eNearest);
  TexturePtr color_tex2b = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        2, vk::Filter::eNearest);
  RenderPassInfo info2a, info2b;
  bool sample_2_supported = false;
  if (depth_tex2 || (color_tex2a && color_tex2b)) {
    InitRenderPassInfo(&info2a, depth_tex2, color_tex2a, color_tex1a);
    InitRenderPassInfo(&info2b, depth_tex2, color_tex2b, color_tex1b);
    sample_2_supported = true;
  }

  // Attachments and renderpass info for 4x MSAA.
  TexturePtr depth_tex4 = escher->NewAttachmentTexture(depth_stencil_texture_format_result.value,
                                                       width, height, 4, vk::Filter::eNearest);
  TexturePtr color_tex4a = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        4, vk::Filter::eNearest);
  TexturePtr color_tex4b = escher->NewAttachmentTexture(vk::Format::eB8G8R8A8Unorm, width, height,
                                                        4, vk::Filter::eNearest);
  RenderPassInfo info4a, info4b;
  bool sample_4_supported = false;
  if (depth_tex4 || (color_tex4a && color_tex4b)) {
    InitRenderPassInfo(&info4a, depth_tex4, color_tex4a, color_tex1a);
    InitRenderPassInfo(&info4b, depth_tex4, color_tex4b, color_tex1b);
    sample_4_supported = true;
  }

  impl::RenderPassPtr rp1a, rp1b, rp2a, rp2b, rp4a, rp4b;
  if (sample_1_supported) {
    rp1a = cache.ObtainRenderPass(info1a, true);
    rp1b = cache.ObtainRenderPass(info1b, true);
  }
  if (sample_2_supported) {
    rp2a = cache.ObtainRenderPass(info2a, true);
    rp2b = cache.ObtainRenderPass(info2b, true);
  }
  if (sample_4_supported) {
    rp4a = cache.ObtainRenderPass(info4a, true);
    rp4b = cache.ObtainRenderPass(info4b, true);
  }

  // Same cached renderpass should be returned for info with the same sample count (but different
  // framebuffer images).
  // We ignore the result if a sample count is not supported.
  EXPECT_TRUE(!sample_1_supported || rp1a == rp1b);
  EXPECT_TRUE(!sample_2_supported || rp2a == rp2b);
  EXPECT_TRUE(!sample_4_supported || rp4a == rp4b);

  // Different cached renderpass should be returned when the sample count differs.
  // We ignore the result if a sample count is not supported.
  EXPECT_TRUE(!sample_1_supported || !sample_2_supported || rp1a != rp2a);
  EXPECT_TRUE(!sample_1_supported || !sample_4_supported || rp1a != rp4a);
  EXPECT_TRUE(!sample_2_supported || !sample_4_supported || rp2a != rp4a);

  EXPECT_TRUE(escher->Cleanup());
}

VK_TEST_F(RenderPassCacheTest, UnexpectedLazyCreationCallback) {
  auto escher = test::GetEscher();

  impl::RenderPassCache cache(escher->resource_recycler());

  // Create two incompatible RenderPassInfos, differing only in the format of the output image.
  RenderPassInfo rpi1, rpi2;
  {
    const vk::Format depth_stencil_format =
        ESCHER_CHECKED_VK_RESULT(escher->device()->caps().GetMatchingDepthStencilFormat(
            {vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint}));

    RenderPassInfo::AttachmentInfo color_attachment_info{
        .format = vk::Format::eB8G8R8A8Unorm,
        .swapchain_layout = vk::ImageLayout::eColorAttachmentOptimal,
        .sample_count = 1,
        .is_transient = false,
    };

    RenderPassInfo::InitRenderPassInfo(&rpi1, color_attachment_info, depth_stencil_format,
                                       vk::Format::eUndefined, 1, false);
    color_attachment_info.format = vk::Format::eR8G8B8A8Unorm;
    RenderPassInfo::InitRenderPassInfo(&rpi2, color_attachment_info, depth_stencil_format,
                                       vk::Format::eUndefined, 1, false);
  }

  FXL_LOG(INFO) << "============= NOTE: Escher warnings expected";

  // No callback has been set and |allow_render_pass_creation| is false.  Default behavior is to
  // return nullptr.
  EXPECT_FALSE(cache.ObtainRenderPass(rpi1, false));
  EXPECT_EQ(0U, cache.size());

  bool callback_was_called = false;
  bool callback_should_allow_lazy_creation = false;
  cache.set_unexpected_lazy_creation_callback([&](const RenderPassInfo& rpi) {
    callback_was_called = true;
    return callback_should_allow_lazy_creation;
  });

  // The callback will answer false, so no render-pass will be created/obtained.
  EXPECT_FALSE(cache.ObtainRenderPass(rpi1, false));
  EXPECT_TRUE(callback_was_called);
  EXPECT_EQ(0U, cache.size());
  // Same thing again, same result.
  callback_was_called = false;
  EXPECT_FALSE(cache.ObtainRenderPass(rpi1, false));
  EXPECT_TRUE(callback_was_called);
  EXPECT_EQ(0U, cache.size());

  // Switch over to the second render-pass, rpi2.
  callback_was_called = false;
  EXPECT_FALSE(cache.ObtainRenderPass(rpi2, false));
  EXPECT_TRUE(callback_was_called);

  // The rest of the test will either allow lazy creation (whether via the argument to
  // ObtainRenderPass(), or via the closure), or the render-pass will already be cached.  Therefore,
  // we expect no warnings after this point.
  FXL_LOG(INFO) << "============= NOTE: no additional Escher warnings are expected\n";

  // Switch back to the first render-pass, rpi1.
  // This time the callback will answer true, so a render-pass will be created/obtained, even
  // though |allow_render_pass_creation| is false.
  EXPECT_EQ(0U, cache.size());
  callback_was_called = false;
  callback_should_allow_lazy_creation = true;
  EXPECT_TRUE(cache.ObtainRenderPass(rpi1, false));
  EXPECT_EQ(1U, cache.size());
  EXPECT_TRUE(callback_was_called);
  callback_was_called = false;
  // Because the render-pass already exists, the callback won't be invoked.
  EXPECT_TRUE(cache.ObtainRenderPass(rpi1, false));
  EXPECT_FALSE(callback_was_called);
  // Changing |allow_render_pass_creation| doesn't matter; it will return the identical render-pass.
  EXPECT_EQ(cache.ObtainRenderPass(rpi1, false), cache.ObtainRenderPass(rpi1, true));

  // Switch back to rpi2 for the last time.  This time we allow lazy creation via
  // |allow_render_pass| instead of the closure; there is no need to call the callback because lazy
  // creation is explicitly allowed by the caller.
  EXPECT_EQ(1U, cache.size());
  callback_was_called = false;
  callback_should_allow_lazy_creation = false;
  EXPECT_TRUE(cache.ObtainRenderPass(rpi2, true));
  EXPECT_FALSE(callback_was_called);
  EXPECT_EQ(2U, cache.size());
  // The render-pass is now cached, so will be found if we change |allow_render_pass| back to false;
  // the callback is still not called.
  EXPECT_TRUE(cache.ObtainRenderPass(rpi2, false));
  EXPECT_FALSE(callback_was_called);
  // As before, verify that we can obtain the same render-pass two ways.
  EXPECT_EQ(cache.ObtainRenderPass(rpi2, false), cache.ObtainRenderPass(rpi2, true));

  // Finally, verify that rpi1 and rpi2 return different render-passes.
  EXPECT_NE(cache.ObtainRenderPass(rpi1, true), cache.ObtainRenderPass(rpi2, true));
}

}  // anonymous namespace
