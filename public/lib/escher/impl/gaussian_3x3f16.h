// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include "lib/escher/forward_declarations.h"
#include "lib/escher/impl/compute_shader.h"
#include "lib/fxl/macros.h"
#include "vulkan/vulkan.hpp"

namespace escher {
namespace impl {

// A helper class that wraps up a compute shader for Gaussian blur on images
// with vk::Format::eR16G16B16A16Sfloat.
// TODO(SCN-619): Investigate the performance issue.
class Gaussian3x3f16 {
 public:
  explicit Gaussian3x3f16(Escher* escher);

  // Apply two-pass gaussian on the input texture, and render into the output
  // texture. Mipmap is not supported. Assumes the image layout is
  // vk::ImageLayout::eGeneral.ss

  void Apply(CommandBuffer* command_buffer,
             const TexturePtr& input,
             const TexturePtr& output);

  Escher* escher_;
  std::unique_ptr<ComputeShader> kernel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Gaussian3x3f16);
};

}  // namespace impl
}  // namespace escher
