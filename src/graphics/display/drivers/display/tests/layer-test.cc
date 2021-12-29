// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/display/layer.h"

#include <zircon/pixelformat.h>

#include <fbl/auto_lock.h>
#include <zxtest/zxtest.h>

#include "src/graphics/display/drivers/display/controller.h"
#include "src/graphics/display/drivers/display/fence.h"
#include "src/graphics/display/drivers/display/image.h"
#include "src/graphics/display/drivers/display/tests/base.h"
#include "src/graphics/display/drivers/fake/fake-display.h"

namespace fhd = fuchsia_hardware_display;

namespace display {

class LayerTest : public TestBase {
 public:
  fbl::RefPtr<Image> CreateReadyImage() {
    image_t dc_image = {
        .width = kDisplayWidth,
        .height = kDisplayHeight,
        .pixel_format = ZX_PIXEL_FORMAT_RGB_x888,
        .type = fhd::wire::kTypeSimple,
        .handle = 0,
    };
    EXPECT_OK(display()->ImportVmoImage(&dc_image, zx::vmo(0), 0));
    EXPECT_NE(dc_image.handle, 0);
    auto image = fbl::AdoptRef(
        new Image(controller(), dc_image, zx::vmo(0), /*stride=*/0, nullptr, /*client_id=*/1u));
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
  fhd::wire::ImageConfig image_config = {.width = kDisplayWidth,
                                         .height = kDisplayHeight,
                                         .pixel_format = ZX_PIXEL_FORMAT_RGB_x888,
                                         .type = fhd::wire::kTypeSimple};
  fhd::wire::Frame frame = {.width = kDisplayWidth, .height = kDisplayHeight};
  l.SetPrimaryConfig(image_config);
  l.SetPrimaryPosition(fhd::wire::Transform::kIdentity, frame, frame);
  l.SetPrimaryAlpha(fhd::wire::AlphaMode::kDisable, 0);
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
