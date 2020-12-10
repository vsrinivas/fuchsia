// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/geometry/bounding_box.h"
#include "src/ui/lib/escher/material/material.h"
#include "src/ui/lib/escher/paper/paper_renderer.h"
#include "src/ui/lib/escher/paper/paper_renderer_static_config.h"
#include "src/ui/lib/escher/paper/paper_scene.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/test/common/paper_renderer_test.h"
#include "src/ui/lib/escher/types/color.h"
#include "src/ui/lib/escher/types/color_histogram.h"
#include "src/ui/lib/escher/util/image_utils.h"

#include <vulkan/vulkan.hpp>

namespace escher {
namespace test {

namespace {

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

using GammaCorrectionTest = PaperRendererTest;

// We draw the following scene:
// +--------+---------+---------+---------+
// |                  ǁ                   |
// |                  ǁ                   |
// |                  ǁ                   |
// +                  ǁ                   |
// |                  ǁ                   |
// |                  ǁ                   |
// |        not       ǁ                   |
// +       gamma      ǁ       gamma       |
// |     corrected    ǁ     corrected     |
// |                  ǁ                   |
// |                  ǁ                   |
// +                  ǁ                   |
// |                  ǁ                   |
// |                  ǁ                   |
// |                  ǁ                   |
// +--------+---------+---------+---------+
//
// A rectangle and a square a drawn, both with the same color texture, with gamma
// correction applied only to the rectangle.  The rectangle is drawn above the square,
// on the right half of the output image.
//
//  1/2 of the area should be kColor (1, .8, .4, 1);
//  1/2 of the area should be gamma-corrected kColor (1, .64, .16, 1);
//     (i.e. the RGB components are squared)
VK_TEST_F(GammaCorrectionTest, SomeCorrectedSomeNot) {
  const glm::vec4 kColor(1.f, 0.8f, 0.4f, 1);
  // Gamma-corrected color is (1, .64, .16, 1).
  // 0-255 integer representations of these colors are:
  const ColorBgra kExpectedNonGammaColor = ColorBgra::FromFloats(kColor);
  const ColorBgra kExpectedGammaColor = ColorBgra::FromFloats(kColor * kColor);

  MaterialPtr material;
  {
    auto gpu_uploader = std::make_unique<escher::BatchGpuUploader>(escher(), 0);
    ImagePtr image =
        escher()->NewRgbaImage(gpu_uploader.get(), 1, 1, ColorRgba::FromFloats(kColor).bytes());
    gpu_uploader->Submit();
    escher()->vk_device().waitIdle();
    TexturePtr tex = Texture::New(escher()->resource_recycler(), image, vk::Filter::eNearest);
    material = Material::New(glm::vec4(1, 1, 1, 1), tex);
  }

  SetupFrame();
  BeginRenderingFrame();
  escher::PaperTransformStack* transform_stack = renderer()->transform_stack();

  transform_stack->PushTranslation(vec2(0, 0));
  {
    transform_stack->PushElevation(0);
    vec2 top_left(0, 0);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    renderer()->DrawRect(top_left, bottom_right, material);
    transform_stack->Pop();
  }
  {
    transform_stack->PushElevation(-1);
    vec2 top_left(kFramebufferWidth / 2, 0);
    vec2 bottom_right(kFramebufferWidth, kFramebufferHeight);
    renderer()->DrawRect(top_left, bottom_right, material, PaperDrawableFlagBits::kBt709Oetf);
    transform_stack->Pop();
  }

  EndRenderingFrame();
  escher()->vk_device().waitIdle();

  auto bytes = GetPixelData();
  const ColorHistogram<ColorBgra> histogram(bytes.data(), kFramebufferWidth * kFramebufferHeight);

  auto expected_histogram = ColorHistogram<ColorBgra>(
      {{kExpectedNonGammaColor, kFramebufferWidth * kFramebufferHeight / 2},
       {kExpectedGammaColor, kFramebufferWidth * kFramebufferHeight / 2}});

  EXPECT_RESULT_OK(ExpectHistogramMatch(histogram, expected_histogram));

  frame_data().frame->EndFrame(SemaphorePtr(), []() {});
}

}  // namespace test
}  // namespace escher
