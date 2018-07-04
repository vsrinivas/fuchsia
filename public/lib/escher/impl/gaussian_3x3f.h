// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_IMPL_GAUSSIAN_3X3F_H_
#define LIB_ESCHER_IMPL_GAUSSIAN_3X3F_H_

#include <memory>
#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/compute_shader.h"
#include "lib/fxl/macros.h"
#include "vulkan/vulkan.hpp"

namespace escher {
namespace impl {

// A helper class that wraps up a compute shader for Gaussian blur on images
// with vk::Format::eR32G32B32A32Sfloat.
// TODO(SCN-619): Investigate the performance issue.
class Gaussian3x3f {
 public:
  explicit Gaussian3x3f(EscherWeakPtr escher);

  // Apply two-pass gaussian on the input texture, and render into the output
  // texture. Mipmap is not supported. Assumes the image layout is
  // vk::ImageLayout::eGeneral.
  void Apply(CommandBuffer* command_buffer, const TexturePtr& input,
             const TexturePtr& output);

 private:
  EscherWeakPtr escher_;
  std::unique_ptr<ComputeShader> kernel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Gaussian3x3f);
};

}  // namespace impl
}  // namespace escher

#endif  // LIB_ESCHER_IMPL_GAUSSIAN_3X3F_H_
