// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_DEPTH_TO_COLOR_H_
#define LIB_ESCHER_UTIL_DEPTH_TO_COLOR_H_

#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/compute_shader.h"

namespace escher {

// Utility that uses a compute shader to transform a depth image to a color
// image.  One common use-case is for debugging, since Vulkan does not support
// directly bliting a depth image into a color image.
class DepthToColor {
 public:
  DepthToColor(EscherWeakPtr escher, ImageFactory* image_factory);

  TexturePtr Convert(const FramePtr& frame, const TexturePtr& depth_texture,
                     vk::ImageUsageFlags image_flags);

 private:
  const EscherWeakPtr escher_;
  ImageFactory* const image_factory_;
  std::unique_ptr<impl::ComputeShader> kernel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DepthToColor);
};

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_DEPTH_TO_COLOR_H_
