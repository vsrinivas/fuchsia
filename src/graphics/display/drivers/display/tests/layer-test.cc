// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../layer.h"

#include <zircon/pixelformat.h>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "../../fake/fake-display.h"
#include "../controller.h"
#include "../fence.h"
#include "../image.h"
#include "base.h"

namespace fhd = ::llcpp::fuchsia::hardware::display;

namespace display {

class LayerTest : public TestBase {
 public:
  fbl::RefPtr<Image> CreateReadyImage() {
    image_t dc_image = {
        .width = kDisplayWidth,
        .height = kDisplayHeight,
        .pixel_format = ZX_PIXEL_FORMAT_RGB_x888,
        .type = fhd::TYPE_SIMPLE,
        .handle = 0,
    };
    EXPECT_OK(display()->ImportVmoImage(&dc_image, zx::vmo(0), 0));
    EXPECT_NE(dc_image.handle, 0);
    auto image = fbl::AdoptRef(new Image(controller(), dc_image, zx::vmo(0), /*stride=*/0));
    image->id = next_image_id_++;
    image->Acquire();
    return image;
  }

 protected:
  static constexpr uint32_t kDisplayWidth = 1024;
  static constexpr uint32_t kDisplayHeight = 600;

  uint64_t next_image_id_ = 1;
};

TEST_F(LayerTest, PrimaryBasic) {
  Layer l(1);
  fhd::ImageConfig image_config = {.width = kDisplayWidth,
                                   .height = kDisplayHeight,
                                   .pixel_format = ZX_PIXEL_FORMAT_RGB_x888,
                                   .type = fhd::TYPE_SIMPLE};
  fhd::Frame frame = {.width = kDisplayWidth, .height = kDisplayHeight};
  l.SetPrimaryConfig(image_config);
  l.SetPrimaryPosition(fhd::Transform::IDENTITY, frame, frame);
  l.SetPrimaryAlpha(fhd::AlphaMode::DISABLE, 0);
  auto image = CreateReadyImage();
  l.SetImage(image, INVALID_ID, INVALID_ID);
  l.ApplyChanges({.h_addressable = kDisplayWidth, .v_addressable = kDisplayHeight});
}

TEST_F(LayerTest, CursorBasic) {
  Layer l(1);
  l.SetCursorConfig({});
  l.SetCursorPosition(/*x=*/4, /*y=*/10);
  auto image = CreateReadyImage();
  l.SetImage(image, INVALID_ID, INVALID_ID);
  l.ApplyChanges({.h_addressable = kDisplayWidth, .v_addressable = kDisplayHeight});
}

}  // namespace display
