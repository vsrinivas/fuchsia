// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_SVG_SCENE_DEMO_IMAGE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_SVG_SCENE_DEMO_IMAGE_H_

#include <functional>
#include <vector>

#include "common/demo_image.h"
#include "spinel/spinel_types.h"
#include "svg/svg.h"
#include "tests/common/affine_transform.h"
#include "tests/common/svg/svg_scene.h"

// Models a spinel image of the scene after an optional transform has been
// applied.
class SvgSceneDemoImage : public DemoImage {
 public:
  using FrameTransformFunc = std::function<spn_transform_t(uint32_t)>;

  class Parent;  // Used internally to store shared state between all images.

  SvgSceneDemoImage(Parent * parent, uint32_t clip_width, uint32_t clip_height);

  virtual ~SvgSceneDemoImage();

  // Prepare image for rendering.
  void
  setup(uint32_t frame_counter) override;

  // Render image.
  void
  render(void * submit_ext, uint32_t width, uint32_t height) override;

  // Ensure image is flushed to swapchain.
  void
  flush() override;

  // Return an image factory corresponding to a given scene and
  // frame transform function.
  static DemoImage::Factory
  makeFactory(const SvgScene & scene, FrameTransformFunc transform_func);

 private:
  void
  resetRasters();
  void
  resetLayers();

  Parent *                  parent_;
  spn_context_t             context_;
  std::vector<spn_raster_t> rasters_;
  spn_composition_t         composition_ = nullptr;
  spn_styling_t             styling_     = nullptr;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_SVG_SCENE_DEMO_IMAGE_H_
