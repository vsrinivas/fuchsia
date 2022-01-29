// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/debug/debug_rects.h"
#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/material/material.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/paper/paper_timestamp_graph.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/test/common/paper_renderer_test.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/types/color_histogram.h"
#include "src/ui/lib/escher/util/image_utils.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

namespace {

constexpr vk::Format kYuv420Nv12ImageFormat = vk::Format::eG8B8R82Plane420Unorm;
constexpr vk::ImageTiling kYuvTextureTiling = vk::ImageTiling::eOptimal;

bool IsImageFormatSupported(vk::PhysicalDevice device, vk::Format format, vk::ImageTiling tiling) {
  vk::FormatProperties properties = device.getFormatProperties(format);
  if (tiling == vk::ImageTiling::eLinear) {
    return properties.linearTilingFeatures != vk::FormatFeatureFlags();
  } else {
    return properties.optimalTilingFeatures != vk::FormatFeatureFlags();
  }
}

ImagePtr CreateYuv420Nv12Image(Escher* escher, size_t width, size_t height, ColorSpace color_space,
                               std::array<uint8_t, 3> yuv) {
  auto gpu_uploader = std::make_unique<escher::BatchGpuUploader>(escher->GetWeakPtr(), 0);
  auto image = image_utils::NewImage(escher->image_cache(), kYuv420Nv12ImageFormat, color_space,
                                     width, height, vk::ImageUsageFlags());

  std::vector<uint8_t> bytes(width * height, yuv[0]);
  bytes.reserve(width * height * 3 / 2);
  for (size_t i = 0; i < width * height / 4; i++) {
    bytes.push_back(yuv[1]);
    bytes.push_back(yuv[2]);
  }
  image_utils::WritePixelsToImage(gpu_uploader.get(), bytes.data(), image,
                                  vk::ImageLayout::eShaderReadOnlyOptimal);

  gpu_uploader->Submit();
  ESCHER_DCHECK_VK_RESULT(escher->vk_device().waitIdle());
  return image;
}

template <class ColorT>
bool ColorMatch(const ColorT x, const ColorT y, double eps = 0.05) {
  static_assert(ColorT::color_depth() == 8);
  return sqrt(((x.r - y.r) / 255.0 * (x.r - y.r) / 255.0) +
              ((x.g - y.g) / 255.0 * (x.g - y.g) / 255.0) +
              ((x.b - y.b) / 255.0 * (x.b - y.b) / 255.0) +
              ((x.a - y.a) / 255.0 * (x.a - y.a) / 255.0)) < eps;
}

ColorBgra ColorAt(const uint8_t* bgra_data, size_t stride, size_t col, size_t row) {
  const uint8_t* ptr = bgra_data + (row * stride + col) * sizeof(ColorBgra);
  ColorBgra result{0, 0, 0, 0};
  result.b = *ptr;
  result.g = *(ptr + 1);
  result.r = *(ptr + 2);
  result.a = *(ptr + 3);
  return result;
}

}  // namespace

using YuvIntegrationTest = PaperRendererTest;

// We draw the following scene:
// +--------+---------+---------+---------+
// |                  |                   |
// |                  |                   |
// |     Rec 709      |     Rec 601       |
// +                  |                   |
// | Y=128 U=96 V=160 | Y=128 U=96 V=160  |
// |                  |                   |
// |                  |                   |
// +--------------------------------------|
// |                  |                   |
// |                  |                   |
// |   Rec 601 Wide   |                   |
// +                  |                   |
// | Y=128 U=96 V=160 |                   |
// |                  |                   |
// |                  |                   |
// +--------+---------+---------+---------+
//
// This test verifies that the Escher could sample YUV images using their
// corresponding color space type and color space range.
VK_TEST_F(YuvIntegrationTest, Rec709Texture) {
  const uint8_t kY = 128;
  const uint8_t kU = 96;
  const uint8_t kV = 160;

  if (escher()->device()->caps().allow_ycbcr &&
      IsImageFormatSupported(escher()->vk_physical_device(), kYuv420Nv12ImageFormat,
                             kYuvTextureTiling)) {
    FX_LOGS(INFO) << "YCbCr format is not supported by the Vulkan device. Test skipped.";
    GTEST_SKIP();
  }

  ImagePtr rec709_image =
      CreateYuv420Nv12Image(escher().get(), kFramebufferWidth / 2, kFramebufferHeight / 2,
                            ColorSpace::kRec709, {kY, kU, kV});
  TexturePtr rec709_texture =
      Texture::New(escher()->resource_recycler(), rec709_image, vk::Filter::eNearest);
  MaterialPtr rec709_material = Material::New(vec4(1, 1, 1, 1), rec709_texture);

  ImagePtr rec601_image =
      CreateYuv420Nv12Image(escher().get(), kFramebufferWidth / 2, kFramebufferHeight / 2,
                            ColorSpace::kRec601Ntsc, {kY, kU, kV});
  TexturePtr rec601_texture =
      Texture::New(escher()->resource_recycler(), rec601_image, vk::Filter::eNearest);
  MaterialPtr rec601_material = Material::New(vec4(1, 1, 1, 1), rec601_texture);

  ImagePtr rec601_wide_image =
      CreateYuv420Nv12Image(escher().get(), kFramebufferWidth / 2, kFramebufferHeight / 2,
                            ColorSpace::kRec601NtscFullRange, {kY, kU, kV});
  TexturePtr rec601_wide_texture =
      Texture::New(escher()->resource_recycler(), rec601_wide_image, vk::Filter::eNearest);
  MaterialPtr rec601_wide_material = Material::New(vec4(1, 1, 1, 1), rec601_wide_texture);

  SetupFrame();
  BeginRenderingFrame();
  escher::PaperTransformStack* transform_stack = renderer()->transform_stack();

  transform_stack->PushTranslation(vec2(0, 0));
  {
    transform_stack->PushElevation(0);
    {
      vec2 top_left(0, 0);
      vec2 bottom_right(kFramebufferWidth / 2, kFramebufferHeight / 2);
      renderer()->DrawRect(top_left, bottom_right, rec709_material);
    }
    {
      vec2 top_left(kFramebufferWidth / 2, 0);
      vec2 bottom_right(kFramebufferWidth, kFramebufferHeight / 2);
      renderer()->DrawRect(top_left, bottom_right, rec601_material);
    }
    {
      vec2 top_left(0, kFramebufferHeight / 2);
      vec2 bottom_right(kFramebufferWidth / 2, kFramebufferHeight);
      renderer()->DrawRect(top_left, bottom_right, rec601_wide_material);
    }
    transform_stack->Pop();
  }

  EndRenderingFrame();
  EXPECT_VK_SUCCESS(escher()->vk_device().waitIdle());

  auto bytes = GetPixelData();
  auto color_at_709 =
      ColorAt(bytes.data(), kFramebufferWidth, kFramebufferWidth / 4, kFramebufferHeight / 4);
  auto color_at_601 =
      ColorAt(bytes.data(), kFramebufferWidth, kFramebufferWidth * 3 / 4, kFramebufferHeight / 4);
  auto color_at_601_wide =
      ColorAt(bytes.data(), kFramebufferWidth, kFramebufferWidth / 4, kFramebufferHeight * 3 / 4);

  // TODO(fxbug.dev/65765): We should check the exact color values once we have
  // a good explanation for the converted RGB values.
  EXPECT_FALSE(ColorMatch(color_at_709, color_at_601));
  EXPECT_FALSE(ColorMatch(color_at_709, color_at_601_wide));
  EXPECT_FALSE(ColorMatch(color_at_601, color_at_601_wide));

  TeardownFrame();
}

}  // namespace test
}  // namespace escher
