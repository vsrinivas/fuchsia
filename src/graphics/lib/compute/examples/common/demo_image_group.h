// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_IMAGE_GROUP_H_
#define SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_IMAGE_GROUP_H_

#include <vector>

#include "demo_image.h"

// A DemoImageGroup models a small array of DemoImage instance used
// as rendering targets by out demo programs, as well as a circular
// cursor pointing to the "current" image.
//
class DemoImageGroup {
 public:
  void
  setFactory(const DemoImage::Factory & factory)
  {
    factory_ = factory;
  }

  // Prepare |config.image_count| images for rendering.
  void
  setup(const DemoImage::Config config)
  {
    for (uint32_t nn = 0; nn < config.image_count; ++nn)
      images_.push_back(factory_(config));
  }

  uint32_t
  current_index() const
  {
    return current_;
  }

  // Return reference to the next rendering image, incrementing the cursor
  // before returning.
  DemoImage &
  getNextImage(uint32_t * frame_index)
  {
    *frame_index = current_;
    current_     = (current_ + 1) % images_.size();
    return *images_[*frame_index];
  }

  // Return reference to current rendering image.
  DemoImage &
  getPreviousImage()
  {
    size_t prev = current_ > 0 ? current_ - 1 : images_.size() - 1u;
    return *images_[prev];
  }

  // Dispose of all resources.
  void
  teardown()
  {
    images_.clear();
    factory_ = nullptr;
  }

 protected:
  DemoImage::Factory                      factory_;
  std::vector<std::unique_ptr<DemoImage>> images_;
  size_t                                  current_ = 0;
};

#endif  // SRC_GRAPHICS_LIB_COMPUTE_EXAMPLES_COMMON_DEMO_IMAGE_GROUP_H_
