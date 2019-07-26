// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test.h"

namespace {

uint32_t interpolate(uint32_t val, uint32_t cur_limit, uint32_t new_limit) {
  // Interploates from [0, cur_limit) to [0, new_limit)
  return ((val * (new_limit - 1)) + cur_limit - 2) / (cur_limit - 1);
}

}  // namespace

namespace display_test {

DISPLAY_TEST(two_layer_test_dest_frame, [](Context* context) {
  auto layer1 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
  auto image1 = context->CreateImage(context->display_width(), context->display_height());
  layer1->SetImage(image1);

  auto layer2 =
      context->CreatePrimaryLayer(context->display_width(), context->display_height() / 2);
  auto image2 = context->CreateImage(context->display_width(), context->display_height() / 2);
  layer2->SetPosition(fuchsia::hardware::display::Transform::IDENTITY,
                      {0, 0, image2->width(), image2->height()},
                      {0, context->display_height() / 4, image2->width(), image2->height()});
  layer2->SetImage(image2);

  context->SetLayers(std::vector<Layer*>({layer1, layer2}));
  context->ApplyConfig();
});

DISPLAY_TEST(two_layer_test_dest_frame_progressive, [](Context* context) {
  auto layer1 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
  auto image1 = context->CreateImage(context->display_width(), context->display_height());
  layer1->SetImage(image1);

  auto layer2 =
      context->CreatePrimaryLayer(context->display_width() / 2, context->display_height() / 2);
  auto image2 = context->CreateImage(context->display_width() / 2, context->display_height() / 2);
  layer2->SetImage(image2);

  context->SetLayers(std::vector<Layer*>({layer1, layer2}));

  constexpr uint32_t kIterCount = 8;
  uint32_t width_limit = context->display_width() - image2->width();
  uint32_t height_limit = context->display_height() - image2->height();
  for (unsigned i = 0; i < kIterCount; i++) {
    uint32_t x = interpolate(i, kIterCount, width_limit);
    uint32_t y = interpolate(i, kIterCount, height_limit);
    layer2->SetPosition(fuchsia::hardware::display::Transform::IDENTITY,
                        {0, 0, image2->width(), image2->height()},
                        {x, y, image2->width(), image2->height()});
    context->ApplyConfig();
  }
});

DISPLAY_TEST(single_layer_src_frame, [](Context* context) {
  static const uint32_t offset = 10;
  auto layer = context->CreatePrimaryLayer(context->display_width() + offset,
                                           context->display_height() + offset);
  auto image =
      context->CreateImage(context->display_width() + offset, context->display_height() + offset);
  layer->SetPosition(fuchsia::hardware::display::Transform::IDENTITY,
                     {offset, offset, context->display_width(), context->display_height()},
                     {0, 0, context->display_width(), context->display_height()});
  layer->SetImage(image);

  context->SetLayers(std::vector<Layer*>({layer}));
  context->ApplyConfig();
});

DISPLAY_TEST(single_layer_src_frame_progressive, [](Context* context) {
  constexpr uint32_t kNumIters = 8;
  constexpr uint32_t kStepSize = 10;
  constexpr uint32_t kExtraSize = (kNumIters - 1) * kStepSize;

  auto layer = context->CreatePrimaryLayer(context->display_width() + kExtraSize,
                                           context->display_height() + kExtraSize);
  auto image = context->CreateImage(context->display_width() + kExtraSize,
                                    context->display_height() + kExtraSize);
  layer->SetImage(image);
  context->SetLayers(std::vector<Layer*>({layer}));

  for (unsigned i = 0; i < kNumIters; i++) {
    uint32_t offset = i * kStepSize;
    layer->SetPosition(fuchsia::hardware::display::Transform::IDENTITY,
                       {offset, offset, context->display_width(), context->display_height()},
                       {0, 0, context->display_width(), context->display_height()});
    context->ApplyConfig();
  }
});

Test rotation_test(fuchsia::hardware::display::Transform mode) {
  return [mode](Context* context) {
    auto layer = context->CreatePrimaryLayer(context->display_width(), context->display_height());
    auto image = context->CreateImage(context->display_width(), context->display_height());
    layer->SetPosition(mode, {0, 0, context->display_width(), context->display_height()},
                       {0, 0, context->display_width(), context->display_height()});
    layer->SetImage(image);

    context->SetLayers(std::vector<Layer*>({layer}));
    context->ApplyConfig();
  };
}

DISPLAY_TEST(rotate_90_test, rotation_test(fuchsia::hardware::display::Transform::ROT_90));
DISPLAY_TEST(rotate_180_test, rotation_test(fuchsia::hardware::display::Transform::ROT_180));
DISPLAY_TEST(rotate_270_test, rotation_test(fuchsia::hardware::display::Transform::ROT_270));
DISPLAY_TEST(rotate_90_reflectx_test,
             rotation_test(fuchsia::hardware::display::Transform::ROT_90_REFLECT_X));
DISPLAY_TEST(rotate_90_reflecty_test,
             rotation_test(fuchsia::hardware::display::Transform::ROT_90_REFLECT_Y));
DISPLAY_TEST(rotate_reflectx_test, rotation_test(fuchsia::hardware::display::Transform::REFLECT_X));
DISPLAY_TEST(rotate_reflecty_test, rotation_test(fuchsia::hardware::display::Transform::REFLECT_Y));

DISPLAY_TEST(scale_up_test, [](Context* context) {
  auto layer =
      context->CreatePrimaryLayer(context->display_width() / 2, context->display_height() / 2);
  auto image =
      context->CreateScalableImage(context->display_width() / 2, context->display_height() / 2);
  layer->SetPosition(fuchsia::hardware::display::Transform::IDENTITY,
                     {0, 0, image->width(), image->height()},
                     {0, 0, context->display_width(), context->display_height()});
  layer->SetImage(image);

  context->SetLayers(std::vector<Layer*>({layer}));
  context->ApplyConfig();
});

DISPLAY_TEST(scale_down_test, [](Context* context) {
  auto layer =
      context->CreatePrimaryLayer(context->display_width() * 2, context->display_height() * 2);
  auto image =
      context->CreateScalableImage(context->display_width() * 2, context->display_height() * 2);
  layer->SetPosition(fuchsia::hardware::display::Transform::IDENTITY,
                     {0, 0, image->width(), image->height()},
                     {0, 0, context->display_width(), context->display_height()});
  layer->SetImage(image);

  context->SetLayers(std::vector<Layer*>({layer}));
  context->ApplyConfig();
});

DISPLAY_TEST(variable_scale_test, [](Context* context) {
  auto layer1 = context->CreatePrimaryLayer(context->display_width(), context->display_height());
  auto image1 = context->CreateImage(context->display_width(), context->display_height());
  layer1->SetImage(image1);

  auto layer2 =
      context->CreatePrimaryLayer(context->display_width() / 2, context->display_height() / 2);
  auto image2 =
      context->CreateScalableImage(context->display_width() / 2, context->display_height() / 2);
  layer2->SetImage(image2);
  context->SetLayers(std::vector<Layer*>({layer1, layer2}));

  // Scale from .5 to 2x
  constexpr uint32_t kIterCount = 8;
  uint32_t min_width = context->display_width() / 4;
  uint32_t min_height = context->display_height() / 4;
  uint32_t width_range = context->display_width() - min_width + 1;
  uint32_t height_range = context->display_height() - min_height + 1;
  for (unsigned i = 0; i < kIterCount; i++) {
    uint32_t width = interpolate(i, kIterCount, width_range) + min_width;
    uint32_t height = interpolate(i, kIterCount, height_range) + min_height;
    layer2->SetPosition(fuchsia::hardware::display::Transform::IDENTITY,
                        {0, 0, image2->width(), image2->height()}, {0, 0, width, height});
    context->ApplyConfig();
  }
});

// TODO(stevensd): test composition of motion and scaling

}  // namespace display_test
