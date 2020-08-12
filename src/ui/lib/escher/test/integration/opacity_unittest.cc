// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/debug/debug_rects.h"
#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
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

ImagePtr CreateImageFrom1x1RgbaBytes(Escher* escher, std::array<uint8_t, 4> bytes) {
  auto gpu_uploader = std::make_unique<escher::BatchGpuUploader>(escher->GetWeakPtr(), 0);
  ImagePtr image = escher->NewRgbaImage(gpu_uploader.get(), 1, 1, bytes.data());
  gpu_uploader->Submit();
  escher->vk_device().waitIdle();
  return image;
}

struct Result {
  static const Result Ok() { return Result(); }
  static const Result Err(std::string err) { return Result(err); }

  Result() : err(std::nullopt) {}
  Result(std::string err_) : err(err_) {}
  bool IsOk() { return !err.has_value(); }
  bool IsErr() { return err.has_value(); }

  std::optional<std::string> err;
};

template <class ColorT>
bool ColorMatch(const ColorT x, const ColorT y, double eps = 0.05) {
  return sqrt(((x.r - y.r) / 255.0 * (x.r - y.r) / 255.0) +
              ((x.g - y.g) / 255.0 * (x.g - y.g) / 255.0) +
              ((x.b - y.b) / 255.0 * (x.b - y.b) / 255.0) +
              ((x.a - y.a) / 255.0 * (x.a - y.a) / 255.0)) < eps;
}

template <class ColorT>
Result ExpectHistogramMatch(const ColorHistogram<ColorT>& hist_real,
                            const ColorHistogram<ColorT>& hist_expected, double eps = 1e-4) {
  std::vector<ColorT> colors_real;
  std::vector<ColorT> colors_expected;
  size_t total_pixels_real = 0;
  size_t total_pixels_expected = 0;

  for (const auto kv : hist_real.values) {
    colors_real.push_back(kv.first);
    total_pixels_real += kv.second;
  }
  for (const auto kv : hist_expected.values) {
    colors_expected.push_back(kv.first);
    total_pixels_expected += kv.second;
  }

  for (const auto kv : hist_real.values) {
    auto expected_key =
        std::find_if(colors_expected.begin(), colors_expected.end(),
                     [color_x = kv.first](ColorT color_y) { return ColorMatch(color_x, color_y); });
    if (expected_key == colors_expected.end()) {
      std::ostringstream err;
      err << "Color " << kv.first << " not found. \n"
          << "Histogram: " << hist_real << ";\n"
          << "Expected: " << hist_expected;
      return Result::Err(err.str());
    }

    double ratio_expected = hist_expected[*expected_key] / double(total_pixels_expected);
    double ratio_real = kv.second / double(total_pixels_real);

    if (fabs(ratio_real - ratio_expected) > eps) {
      std::ostringstream err;
      err << "Ratio of color " << kv.first << " doesn't match."
          << "  Expected ratio: " << hist_expected[*expected_key] << "/"
          << double(total_pixels_expected) << ", real ratio: " << kv.second << "/"
          << double(total_pixels_real) << "\n"
          << "Histogram: " << hist_real << ";\n"
          << "Expected: " << hist_expected;
      return Result::Err(err.str());
    }
  }
  return Result::Ok();
}

#define EXPECT_RESULT_OK(expr)                 \
  do {                                         \
    Result result = (expr);                    \
    EXPECT_TRUE(result.IsOk()) << *result.err; \
  } while (0);

}  // namespace

using OpacityShapeTest = PaperRendererTest;

// We draw the following scene:
// +--------+---------+---------+---------+
// |                            |         |
// |                            |         |
// |                            |         |
// +      YELLOW                |  BLACK  |
// |    (1, 1, 0, 1)            |         |
// |                            |         |
// |                            |         |
// +                  ====================|
// |                  ǁ         |         |
// |                  ǁ  Blend  |         |
// |                  ǁ         |         |
// +------------------ǁ---------+         |
// |      BLACK       ǁ    Cyan (75%)     |
// |   (0, 0, 0, 1)   ǁ  (0, 1, 1, 0.75)  |
// |                  ǁ                   |
// +--------+---------+---------+---------+
//
// The Cyan rectangle is over Yellow rectangle, which
// is over the black background.
//
//  8/16 of the area should be Yellow (1, 1, 0, 1);
//  4/16 of the area should be Black  (0, 0 ,0, 1);
//  3/16 of the area should be 75% Cyan (0, 0.75, 0.75, 1);
//  1/16 of the area should be the blended color, which is
//    (0.25 * (1, 1, 0) + 0.75 * (0, 1, 1), 1) = (0.25, 1, 0.75, 1).
//
VK_TEST_F(OpacityShapeTest, TranslucentOverOpaque) {
  const glm::vec4 kYellow(1, 1, 0, 1);
  const glm::vec4 kCyan75(0, 1, 1, 0.75);
  const glm::vec4 kBlack(0, 0, 0, 1);

  SetupFrame();
  BeginRenderingFrame();
  escher::PaperTransformStack* transform_stack = renderer()->transform_stack();

  transform_stack->PushTranslation(vec2(0, 0));
  {
    transform_stack->PushElevation(0);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    renderer()->DrawRect(top_left, bottom_right, Material::New(kBlack));
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-1);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth * 3 / 4, kFramebufferHeight * 3 / 4);
    renderer()->DrawRect(top_left, bottom_right, Material::New(kYellow));
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-2);
    vec2 top_left(kFramebufferWidth / 2, kFramebufferHeight / 2);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    MaterialPtr material = Material::New(kCyan75);
    material->set_type(Material::Type::kTranslucent);
    renderer()->DrawRect(top_left, bottom_right, material);
    transform_stack->Pop();
  }

  EndRenderingFrame();
  escher()->vk_device().waitIdle();

  auto bytes = GetPixelData();
  const ColorHistogram<ColorBgra> histogram(bytes.data(), kFramebufferWidth * kFramebufferHeight);

  auto expected_histogram = ColorHistogram<ColorBgra>({{ColorBgra(0xFF, 0xFF, 0x00, 0xFF), 8u},
                                                       {ColorBgra(0x00, 0x00, 0x00, 0xFF), 4u},
                                                       {ColorBgra(0x00, 0xBF, 0xBF, 0xFF), 3u},
                                                       {ColorBgra(0x3F, 0xFF, 0xBF, 0xFF), 1u}});

  EXPECT_RESULT_OK(ExpectHistogramMatch(histogram, expected_histogram));

  frame_data().frame->EndFrame(SemaphorePtr(), []() {});
}

// We draw the following scene:
// +--------+---------+---------+---------+
// |                            |         |
// |                            |         |
// |                            |         |
// +      Cyan(75%)             |  BLACK  |
// |    (0, 1, 1, 0.75)         |         |
// |                            |         |
// |                            |         |
// +                  ====================|
// |                  ǁ                   |
// |                  ǁ                   |
// |                  ǁ      Yellow       |
// +------------------ǁ    (1, 1, 0, 1)   |
// |      BLACK       ǁ                   |
// |   (0, 0, 0, 1)   ǁ                   |
// |                  ǁ                   |
// +--------+---------+---------+---------+
//
// The Yellow rectangle is over Cyan rectangle, which
// is over the black background.
//
//  4/16 of the area should be Yellow (1, 1, 0, 1);
//  4/16 of the area should be Black  (0, 0 ,0, 1);
//  8/16 of the area should be 75% Cyan (0, 0.75, 0.75, 1);
//
VK_TEST_F(OpacityShapeTest, OpaqueOverTranslucent) {
  const glm::vec4 kYellow(1, 1, 0, 1);
  const glm::vec4 kCyan75(0, 1, 1, 0.75);
  const glm::vec4 kBlack(0, 0, 0, 1);

  SetupFrame();
  BeginRenderingFrame();
  escher::PaperTransformStack* transform_stack = renderer()->transform_stack();

  transform_stack->PushTranslation(vec2(0, 0));
  {
    transform_stack->PushElevation(0);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    renderer()->DrawRect(top_left, bottom_right, Material::New(kBlack));
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-2);
    vec2 top_left(kFramebufferWidth / 2, kFramebufferHeight / 2);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    renderer()->DrawRect(top_left, bottom_right, Material::New(kYellow));
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-1);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth * 3 / 4, kFramebufferHeight * 3 / 4);
    MaterialPtr material = Material::New(kCyan75);
    material->set_type(Material::Type::kTranslucent);
    renderer()->DrawRect(top_left, bottom_right, material);
    transform_stack->Pop();
  }

  EndRenderingFrame();
  escher()->vk_device().waitIdle();

  auto bytes = GetPixelData();
  const ColorHistogram<ColorBgra> histogram(bytes.data(), kFramebufferWidth * kFramebufferHeight);

  auto expected_histogram = ColorHistogram<ColorBgra>({{ColorBgra(0xFF, 0xFF, 0x00, 0xFF), 4u},
                                                       {ColorBgra(0x00, 0x00, 0x00, 0xFF), 4u},
                                                       {ColorBgra(0x00, 0xBF, 0xBF, 0xFF), 8u}});

  EXPECT_RESULT_OK(ExpectHistogramMatch(histogram, expected_histogram));

  frame_data().frame->EndFrame(SemaphorePtr(), []() {});
}

// We draw the following scene:
// +--------+---------+---------+---------+
// |                            |         |
// |                            |         |
// |                            |         |
// +      Cyan(25%)             |  WHITE  |
// |    (0, 1, 1, 0.25)         |         |
// |                            |         |
// |                            |         |
// +                  ====================|
// |                  ǁ         |         |
// |                  ǁ  Blend  |         |
// |                  ǁ         |         |
// +------------------ǁ---------+         |
// |     WHITE        ǁ     Yellow 50%    |
// |   (1, 1, 1, 1)   ǁ    (1, 1, 0, 1)   |
// |                  ǁ                   |
// +--------+---------+---------+---------+
//
// The Yellow rectangle is over Cyan rectangle, which
// is over the white background.
//
//  8/16 of the area should be (0.75, 1, 1, 1);
//  4/16 of the area should be (1, 1, 1, 1);
//  3/16 of the area should be (1, 1, 0.5, 1);
//  1/16 of the area should be (0.825, 1, 0.5, 1).
//
VK_TEST_F(OpacityShapeTest, TranslucentOverTranslucent) {
  const glm::vec4 kYellow50(1, 1, 0, 0.5);
  const glm::vec4 kCyan25(0, 1, 1, 0.25);
  const glm::vec4 kWhite(1, 1, 1, 1);

  SetupFrame();
  BeginRenderingFrame();
  escher::PaperTransformStack* transform_stack = renderer()->transform_stack();

  transform_stack->PushTranslation(vec2(0, 0));
  {
    transform_stack->PushElevation(0);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    renderer()->DrawRect(top_left, bottom_right, Material::New(kWhite));
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-1);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth * 3 / 4, kFramebufferHeight * 3 / 4);
    MaterialPtr material = Material::New(kCyan25);
    material->set_type(Material::Type::kTranslucent);
    renderer()->DrawRect(top_left, bottom_right, material);
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-2);
    vec2 top_left(kFramebufferWidth / 2, kFramebufferHeight / 2);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    MaterialPtr material = Material::New(kYellow50);
    material->set_type(Material::Type::kTranslucent);
    renderer()->DrawRect(top_left, bottom_right, material);
    transform_stack->Pop();
  }

  EndRenderingFrame();
  escher()->vk_device().waitIdle();

  auto bytes = GetPixelData();
  const ColorHistogram<ColorBgra> histogram(bytes.data(), kFramebufferWidth * kFramebufferHeight);

  auto expected_histogram = ColorHistogram<ColorBgra>({{ColorBgra(0xBF, 0xFF, 0xFF, 0xFF), 8u},
                                                       {ColorBgra(0xFF, 0xFF, 0xFF, 0xFF), 4u},
                                                       {ColorBgra(0xFF, 0xFF, 0x7F, 0xFF), 3u},
                                                       {ColorBgra(0xD3, 0xFF, 0x7F, 0xFF), 1u}});

  EXPECT_RESULT_OK(ExpectHistogramMatch(histogram, expected_histogram));

  TeardownFrame();
}

// We draw the following scene:
// +--------+---------+---------+---------+
// |                            |         |
// |                            |         |
// |                            |         |
// +      Cyan(25%)             |         |
// |    (0, 0.25, 0.25, 0.25)   |         |
// |                            |         |
// |                            |         |
// +                            |         |
// |                            |         |
// |                            |         |
// |                            |         |
// +----------------------------+         |
// |     Fuchsia                          |
// |   (1, 0, 1, 1)                       |
// |                                      |
// +--------+---------+---------+---------+
//
// The Cyan rectangle uses a premultiplied alpha texture, and it
// is over the white background.
//
//  9/16 of the area should be (0.75, 0.25, 1, 1);
//  7/16 of the area should be (1, 0, 1, 1);
//
// TODO(fxbug.dev/47918): Enable this after premultiplied alpha is supported.
//
VK_TEST_F(OpacityShapeTest, PremultipliedTexture) {
  const glm::vec4 kFuchsia(1, 0, 1, 1);

  std::array<uint8_t, 4> cyan_25_premultiplied_bytes = {0x00, 0x40, 0x40, 0x40};
  ImagePtr cyan_25_image = CreateImageFrom1x1RgbaBytes(escher().get(), cyan_25_premultiplied_bytes);
  TexturePtr cyan_25_texture =
      Texture::New(escher()->resource_recycler(), cyan_25_image, vk::Filter::eNearest);
  MaterialPtr cyan_25_material = Material::New(vec4(1, 1, 1, 1), cyan_25_texture);
  cyan_25_material->set_type(Material::Type::kTranslucent);

  SetupFrame();
  BeginRenderingFrame();
  escher::PaperTransformStack* transform_stack = renderer()->transform_stack();

  transform_stack->PushTranslation(vec2(0, 0));
  {
    transform_stack->PushElevation(0);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    renderer()->DrawRect(top_left, bottom_right, Material::New(kFuchsia));
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-1);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth * 3 / 4, kFramebufferHeight * 3 / 4);
    renderer()->DrawRect(top_left, bottom_right, cyan_25_material);
    transform_stack->Pop();
  }

  EndRenderingFrame();
  escher()->vk_device().waitIdle();

  auto bytes = GetPixelData();
  const ColorHistogram<ColorBgra> histogram(bytes.data(), kFramebufferWidth * kFramebufferHeight);

  auto expected_histogram = ColorHistogram<ColorBgra>(
      {{ColorBgra(0xBF, 0x40, 0xFF, 0xFF), 9u}, {ColorBgra(0xFF, 0x00, 0xFF, 0xFF), 7u}});

  EXPECT_RESULT_OK(ExpectHistogramMatch(histogram, expected_histogram));

  TeardownFrame();
}

}  // namespace test
}  // namespace escher
