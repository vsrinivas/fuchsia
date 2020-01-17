// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_SVG_DEMO_SPINEL_IMAGE_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_SVG_DEMO_SPINEL_IMAGE_H_

#include <vector>

#include "demo_spinel_image.h"
#include "svg/svg.h"
#include "tests/common/scoped_struct.h"
#include "tests/common/spinel/svg_spinel_image.h"

// Wrap an SvgSpinelImage into a DemoSpinelImage instance.
class SvgDemoSpinelImage : public DemoSpinelImage {
 public:
  // Type of a callback used to compute a transform to apply to a given frame
  // based on its counter value.
  using FrameTransformFunc = std::function<spn_transform_t(uint32_t)>;

  // Create new instance. Takes ownership of |svg_image|.
  // |frame_transform_func| is an optional callback that computes a transform to
  // apply to a frame, based on its frame counter.
  SvgDemoSpinelImage(ScopedStruct<SvgSpinelImage> && svg_image,
                     FrameTransformFunc              frame_transform_func = {})
      : svg_image_(std::move(svg_image)), frame_transform_func_(frame_transform_func)
  {
  }

  void
  setupPaths(uint32_t frame_counter) override
  {
    svg_image_->setupPaths();
  }

  void
  setupRasters(uint32_t frame_counter) override
  {
    spn_transform_t transform = { .sx = 1., .sy = 1. };
    if (frame_transform_func_)
      transform = frame_transform_func_(frame_counter);

    svg_image_->setupRasters(&transform);
  }

  void
  setupLayers(uint32_t frame_counter) override
  {
    svg_image_->setupLayers();
  }

  void
  resetPaths() override
  {
    svg_image_->resetPaths();
  }

  void
  resetRasters() override
  {
    svg_image_->resetRasters();
  }

  void
  resetLayers() override
  {
    svg_image_->resetLayers();
  }

  void
  render(void * submit_ext, uint32_t clip_width, uint32_t clip_height) override
  {
    svg_image_->render(submit_ext, clip_width, clip_height);
  }

 protected:
  ScopedStruct<SvgSpinelImage>           svg_image_;
  SvgDemoSpinelImage::FrameTransformFunc frame_transform_func_;
};

// Implement an image provider for SVG images.
// Usage is:
//    1) Create instance, passing a pointer to an input |svg| document,
//       and a frame transform function to display it in a demo.
//
//    2) Pass that to a demo program's constructor.
//
class SvgDemoImageProvider : public DemoSpinelImageProvider {
 public:
  // Create new instance from a given svg document and a custom
  // frame transform func.
  explicit SvgDemoImageProvider(const svg *                            svg,
                                SvgDemoSpinelImage::FrameTransformFunc frame_transform_func)
      : svg_(svg), frame_transform_func_(frame_transform_func)
  {
  }

  ~SvgDemoImageProvider() = default;

  void
  setup(spn_context_t context,
        uint32_t      image_count,
        uint32_t      surface_width,
        uint32_t      surface_height) override
  {
    SpinelImage::Config config = {
      .clip = { 0u, 0u, surface_width, surface_height },
    };
    for (uint32_t nn = 0; nn < image_count; ++nn)
      {
        images_.emplace_back(ScopedStruct<SvgSpinelImage>(svg_, context, config),
                             frame_transform_func_);
      }
  }

  DemoSpinelImage &
  getImage(uint32_t image_index) override
  {
    return images_[image_index];
  }

  void
  teardown() override
  {
    images_.clear();
  }

 protected:
  const svg *                            svg_ = nullptr;
  SvgDemoSpinelImage::FrameTransformFunc frame_transform_func_;
  std::vector<SvgDemoSpinelImage>        images_;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_SVG_DEMO_SPINEL_IMAGE_H_
