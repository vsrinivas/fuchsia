// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/vk/render_pass_info.h"

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/third_party/granite/vk/render_pass.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image_layout_updater.h"
#include "src/ui/lib/escher/vk/impl/render_pass_cache.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace {
using namespace escher;

using RenderPassInfoTest = test::TestWithVkValidationLayer;

namespace {
ImagePtr CreateSwapchainImageWithLayout(Escher* escher, vk::ImageLayout layout) {
  const uint32_t kWidth = 1u;
  const uint32_t kHeight = 1u;
  ImageFactoryAdapter image_factory(escher->gpu_allocator(), escher->resource_recycler());

  ImagePtr result = image_utils::NewColorAttachmentImage(&image_factory, kWidth, kHeight,
                                                         /* additional_flags */ {});
  ImageLayoutUpdater updater(escher->GetWeakPtr());
  if (layout != vk::ImageLayout::eUndefined) {
    updater.ScheduleSetImageInitialLayout(result, layout);
    result->set_swapchain_layout(layout);
  }
  updater.Submit();
  EXPECT_VK_SUCCESS(escher->vk_device().waitIdle());
  return result;
}

// Get a depth stencil texture format supported by the device.
// Returns |eUndefined| if no depth stencil texture is supported.
std::optional<vk::Format> GetSupportedDepthStencilFormat(Escher* escher) {
  std::set<vk::Format> supported_depth_stencil_formats =
      escher->device()->caps().GetAllMatchingDepthStencilFormats(
          {vk::Format::eD16UnormS8Uint, vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint});
  return supported_depth_stencil_formats.empty()
             ? std::make_optional(*supported_depth_stencil_formats.begin())
             : std::nullopt;
}

// Create a depth-stencil texture used for RenderPassInfo.
TexturePtr NewDepthStencilTexture(Escher* escher) {
  const auto supported_format = GetSupportedDepthStencilFormat(escher);
  const uint32_t kWidth = 1u;
  const uint32_t kHeight = 1u;

  if (supported_format) {
    return escher->NewAttachmentTexture(*supported_format, kWidth, kHeight, 1,
                                        vk::Filter::eNearest);
  } else {
    return nullptr;
  }
}

}  // namespace

// Initialize RenderPassInfo with its |output_image| having a valid layout.
VK_TEST_F(RenderPassInfoTest, ValidOutputImageLayout) {
  auto escher = test::GetEscher();

  auto output_image =
      CreateSwapchainImageWithLayout(escher, vk::ImageLayout::eColorAttachmentOptimal);
  TexturePtr color_texture = escher->NewTexture(output_image, vk::Filter::eNearest);
  TexturePtr depth_texture = NewDepthStencilTexture(escher);
  if (!depth_texture) {
    GTEST_SKIP() << "No depth stencil format supported, test terminated.";
  };

  RenderPassInfo info;
  info.op_flags = RenderPassInfo::kOptimalColorLayoutOp;
  info.num_color_attachments = 1;
  info.color_attachments[0] = color_texture;
  info.depth_stencil_attachment = depth_texture;
  RenderPassInfo::InitRenderPassAttachmentInfosFromImages(&info);

  RenderPassInfo render_pass;
  vk::Rect2D render_area = {{0, 0}, {output_image->width(), output_image->height()}};

  EXPECT_TRUE(
      RenderPassInfo::InitRenderPassInfo(&render_pass, render_area, output_image, depth_texture));
}

// Initialize RenderPassInfo with its |output_image| having a layout of |eUndefined|.
// This should fail and error messages in initialization are expected.
VK_TEST_F(RenderPassInfoTest, InvalidOutputImageLayout) {
  auto escher = test::GetEscher();

  auto output_image = CreateSwapchainImageWithLayout(escher, vk::ImageLayout::eUndefined);
  output_image->set_swapchain_layout(vk::ImageLayout::ePresentSrcKHR);
  TexturePtr color_texture = escher->NewTexture(output_image, vk::Filter::eNearest);
  TexturePtr depth_texture = NewDepthStencilTexture(escher);
  if (!depth_texture) {
    GTEST_SKIP() << "No depth stencil format supported, test terminated.";
  };

  RenderPassInfo info;
  info.op_flags = RenderPassInfo::kOptimalColorLayoutOp;
  info.num_color_attachments = 1;
  info.color_attachments[0] = color_texture;
  info.depth_stencil_attachment = depth_texture;
  RenderPassInfo::InitRenderPassAttachmentInfosFromImages(&info);

  RenderPassInfo render_pass;
  vk::Rect2D render_area = {{0, 0}, {output_image->width(), output_image->height()}};

  FX_LOGS(INFO) << "Test RenderPassInfo initialization with invalid image layout, errors expected.";
  EXPECT_FALSE(
      RenderPassInfo::InitRenderPassInfo(&render_pass, render_area, output_image, depth_texture));
}

// Initialize RenderPassInfo with its |output_image| doesn't have a |swapchain_layout|.
// This should fail and error messages in initialization are expected.
VK_TEST_F(RenderPassInfoTest, NonSwapchainOutputImage) {
  auto escher = test::GetEscher();
  ImageFactoryAdapter image_factory(escher->gpu_allocator(), escher->resource_recycler());

  const uint32_t kWidth = 1u;
  const uint32_t kHeight = 1u;
  ImagePtr output_image = image_utils::NewColorAttachmentImage(&image_factory, kWidth, kHeight,
                                                               /* additional_flags */ {});
  EXPECT_VK_SUCCESS(escher->vk_device().waitIdle());

  TexturePtr color_texture = escher->NewTexture(output_image, vk::Filter::eNearest);
  TexturePtr depth_texture = NewDepthStencilTexture(escher);
  if (!depth_texture) {
    GTEST_SKIP() << "No depth stencil format supported, test terminated.";
  };

  RenderPassInfo info;
  info.op_flags = RenderPassInfo::kOptimalColorLayoutOp;
  info.num_color_attachments = 1;
  info.color_attachments[0] = color_texture;
  info.depth_stencil_attachment = depth_texture;
  RenderPassInfo::InitRenderPassAttachmentInfosFromImages(&info);

  RenderPassInfo render_pass;
  vk::Rect2D render_area = {{0, 0}, {output_image->width(), output_image->height()}};

  FX_LOGS(INFO) << "Test RenderPassInfo initialization without swapchain layout, errors expected.";
  EXPECT_FALSE(
      RenderPassInfo::InitRenderPassInfo(&render_pass, render_area, output_image, depth_texture));
}

}  // anonymous namespace
