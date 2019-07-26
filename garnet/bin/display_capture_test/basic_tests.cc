// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test.h"

namespace display_test {

DISPLAY_TEST(single_layer_test, [](Context* context) {
  auto layer = context->CreatePrimaryLayer(context->display_width(), context->display_height());
  auto image = context->CreateImage(context->display_width(), context->display_height());

  layer->SetImage(image);
  context->SetLayers(std::vector<Layer*>({layer}));
  context->ApplyConfig();
});

DISPLAY_TEST(flip_test, [](Context* context) {
  auto layer = context->CreatePrimaryLayer(context->display_width(), context->display_height());
  context->SetLayers(std::vector<Layer*>({layer}));

  for (unsigned i = 0; i < 8; i++) {
    auto image = context->CreateImage(context->display_width(), context->display_height());
    layer->SetImage(image);
    context->ApplyConfig();
  }
});

DISPLAY_TEST(flip_test_reuse_images, [](Context* context) {
  auto layer = context->CreatePrimaryLayer(context->display_width(), context->display_height());
  context->SetLayers(std::vector<Layer*>({layer}));

  auto image1 = context->CreateImage(context->display_width(), context->display_height());
  auto image2 = context->CreateImage(context->display_width(), context->display_height());
  for (unsigned i = 0; i < 8; i++) {
    layer->SetImage(i % 2 ? image1 : image2);
    context->ApplyConfig();
  }
});

}  // namespace display_test
