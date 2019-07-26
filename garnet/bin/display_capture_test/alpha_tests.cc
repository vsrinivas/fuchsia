// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test.h"

#include <cmath>

namespace display_test {

Test per_pixel_test(fuchsia::hardware::display::AlphaMode mode) {
  return [mode](Context* context) {
    auto layer1 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
    auto image1 = context->CreateImage(context->display_width(), context->display_height());
    layer1->SetImage(image1);

    auto layer2 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
    auto image2 =
        context->CreateAlphaImage(context->display_width(), context->display_height(), 0xa0,
                                  mode == fuchsia::hardware::display::AlphaMode::PREMULTIPLIED);
    layer2->SetAlpha(mode, nanf(""));
    layer2->SetImage(image2);

    context->SetLayers(std::vector<Layer*>({layer1, layer2}));
    context->ApplyConfig();
  };
}

DISPLAY_TEST(per_pixel_hwmultiply_test,
             per_pixel_test(fuchsia::hardware::display::AlphaMode::HW_MULTIPLY));
DISPLAY_TEST(per_pixel_premultiply_test,
             per_pixel_test(fuchsia::hardware::display::AlphaMode::PREMULTIPLIED));
DISPLAY_TEST(per_pixel_disable_test,
             per_pixel_test(fuchsia::hardware::display::AlphaMode::DISABLE));

Test plane_test(fuchsia::hardware::display::AlphaMode mode) {
  return [mode](Context* context) {
    auto layer1 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
    auto image1 = context->CreateImage(context->display_width(), context->display_height());
    layer1->SetImage(image1);

    auto layer2 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
    auto image2 = context->CreateImage(context->display_width(), context->display_height());
    layer2->SetAlpha(mode, .62);
    layer2->SetImage(image2);

    context->SetLayers(std::vector<Layer*>({layer1, layer2}));
    context->ApplyConfig();
  };
}

DISPLAY_TEST(plane_alpha_hwmultiply_test,
             plane_test(fuchsia::hardware::display::AlphaMode::HW_MULTIPLY));
DISPLAY_TEST(plane_alpha_premultiply_test,
             plane_test(fuchsia::hardware::display::AlphaMode::PREMULTIPLIED));

Test per_pixel_and_plane_test(fuchsia::hardware::display::AlphaMode mode) {
  return [mode](Context* context) {
    auto layer1 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
    auto image1 = context->CreateImage(context->display_width(), context->display_height());
    layer1->SetImage(image1);

    auto layer2 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
    auto image2 =
        context->CreateAlphaImage(context->display_width(), context->display_height(), 0xa0,
                                  mode == fuchsia::hardware::display::AlphaMode::PREMULTIPLIED);
    layer2->SetAlpha(mode, .4);
    layer2->SetImage(image2);

    context->SetLayers(std::vector<Layer*>({layer1, layer2}));
    context->ApplyConfig();
  };
}

DISPLAY_TEST(per_pixel_and_plane_hwmultiply_test,
             per_pixel_and_plane_test(fuchsia::hardware::display::AlphaMode::HW_MULTIPLY));
DISPLAY_TEST(per_pixel_and_plane_premultiply_test,
             per_pixel_and_plane_test(fuchsia::hardware::display::AlphaMode::PREMULTIPLIED));

DISPLAY_TEST(progressive_plane_alpha_test, [](Context* context) {
  auto layer1 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
  auto image1 = context->CreateImage(context->display_width(), context->display_height());
  layer1->SetImage(image1);

  auto layer2 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
  auto image2 = context->CreateImage(context->display_width(), context->display_height());
  layer2->SetImage(image2);

  context->SetLayers(std::vector<Layer*>({layer1, layer2}));

  for (unsigned i = 0; i <= 5; i++) {
    layer2->SetAlpha(fuchsia::hardware::display::AlphaMode::HW_MULTIPLY, .2 * i);
    context->ApplyConfig();
  }
});

}  // namespace display_test
